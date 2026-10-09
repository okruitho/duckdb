#include "catch.hpp"
#include "duckdb/function/replacement_scan.hpp"
#include "duckdb/main/config.hpp"
#include "duckdb/parser/expression/constant_expression.hpp"
#include "duckdb/parser/expression/function_expression.hpp"
#include "duckdb/parser/tableref/table_function_ref.hpp"
#include "test_helpers.hpp"

using namespace duckdb;

// Claims `name` with range(count), where both come from the scan data.
struct TestReplacementScanData : public ReplacementScanData {
	TestReplacementScanData(string name_p, int64_t count_p) : name(std::move(name_p)), count(count_p) {
	}
	string name;
	int64_t count;
};

static unique_ptr<TableRef> TestRangeReplacement(ClientContext &, ReplacementScanInput &input,
                                                 optional_ptr<ReplacementScanData> data) {
	auto &scan_data = data->Cast<TestReplacementScanData>();
	if (input.table_name != scan_data.name) {
		return nullptr;
	}
	vector<unique_ptr<ParsedExpression>> children;
	children.push_back(ConstantExpression::FromValue(Value::BIGINT(scan_data.count)));
	auto result = make_uniq<TableFunctionRef>();
	result->function = make_uniq<FunctionExpression>("range", std::move(children));
	return std::move(result);
}

static ReplacementScan TestRangeScan(const string &name, int64_t count) {
	return ReplacementScan(TestRangeReplacement, make_uniq<TestReplacementScanData>(name, count));
}

TEST_CASE("Test registering database-wide replacement scans", "[api]") {
	DBConfig config;
	ReplacementScan::Register(config, TestRangeScan("new_before_start", 1));
	DUCKDB_SUPPRESS_DEPRECATED_BEGIN
	config.replacement_scans.push_back(TestRangeScan("legacy_before_start", 2));
	DUCKDB_SUPPRESS_DEPRECATED_END

	DuckDB db(nullptr, &config);
	Connection con(db);
	auto &db_config = DBConfig::GetConfig(*db.instance);
	ReplacementScan::Register(db_config, TestRangeScan("new_after_start", 3));
	DUCKDB_SUPPRESS_DEPRECATED_BEGIN
	db_config.replacement_scans.push_back(TestRangeScan("legacy_after_start", 4));
	DUCKDB_SUPPRESS_DEPRECATED_END

	auto result = con.Query("SELECT count(*) FROM new_before_start");
	REQUIRE(CHECK_COLUMN(result, 0, {1}));
	result = con.Query("SELECT count(*) FROM legacy_before_start");
	REQUIRE(CHECK_COLUMN(result, 0, {2}));
	result = con.Query("SELECT count(*) FROM new_after_start");
	REQUIRE(CHECK_COLUMN(result, 0, {3}));
	result = con.Query("SELECT count(*) FROM legacy_after_start");
	REQUIRE(CHECK_COLUMN(result, 0, {4}));

	// Scans staged before startup still run ahead of the built-in CSV scan.
	ReplacementScan::Register(db_config, TestRangeScan("late.csv", 5));
	DBConfig csv_config;
	ReplacementScan::Register(csv_config, TestRangeScan("early.csv", 6));
	DuckDB csv_db(nullptr, &csv_config);
	Connection csv_con(csv_db);
	result = csv_con.Query("SELECT count(*) FROM 'early.csv'");
	REQUIRE(CHECK_COLUMN(result, 0, {6}));
	REQUIRE_FAIL(con.Query("SELECT count(*) FROM 'late.csv'"));
}
