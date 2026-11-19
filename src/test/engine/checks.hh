#pragma once

#include "test/harness/harness.hh"
#include "engine/database/database.hh"

namespace test {

	bool query ( engine::database& db, const char* sql, result_capture& capture );

	void rows ( engine::database& db, const char* sql, const char* wanted );

	void run ( engine::database& db, const char* sql );

	void fails ( engine::database& db, const char* sql, const char* fragment );

	void plan_has ( engine::database& db, const char* sql, const char* fragment );

	void plan_lacks ( engine::database& db, const char* sql, const char* fragment );

	bool contains ( const char* text, const char* part );

	void savepoint_tests ( engine::database& db, const char* path );

	void join_fuzz ( engine::database& db );

	void join_order_tests ( engine::database& db );

	void join_order_fuzz ( engine::database& db );

	void vacuum_tests ( engine::database& db, const char* path );

	void search_tests ( engine::database& db );

	void check_tests ( engine::database& db );

}