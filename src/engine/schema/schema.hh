#pragma once

#include "engine/run/run.hh"

namespace engine {

	bool reserved_name ( const char* name );

	bool name_taken ( run_context& ctx, const char* name, bool if_not_exists, bool& skip );

	bool uses_query ( const sql::expression* item );

	bool plain_value ( const sql::expression* item );

	bool refresh_schema ( run_context& ctx );

	void index_sql ( base::text& out, bool unique, const char* name, const char* table, const char* const* columns, uint32_t count );

	bool validate_definition ( run_context& ctx, sql::create_table_statement* definition, table_info& info );

	bool write_definition ( run_context& ctx, const sql::create_table_statement* definition, uint32_t root, int64_t& rowid );

	bool mentions_column ( const sql::expression* item, const char* table, const char* column );

}