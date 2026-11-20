#pragma once

#include "engine/run/run.hh"

namespace engine {

	struct sort_key {

		uint64_t head;
		int64_t rowid;
		const uint8_t* data;
		uint32_t size;
		uint8_t rank;
		uint8_t kind;
		bool exact;
		bool nulls;

	};

	bool fill_index ( run_context& ctx, table_info& table, index_info& index, storage::btree& tree );

	bool reserved_name ( const char* name );

	bool name_taken ( run_context& ctx, const char* name, bool if_not_exists, bool& skip );

	bool uses_query ( const sql::expression* item );

	bool plain_value ( const sql::expression* item );

	bool refresh_schema ( run_context& ctx );

	void index_sql ( base::text& out, bool unique, bool fulltext, const char* name, const char* table, const char* const* columns, uint32_t count );

	bool validate_definition ( run_context& ctx, sql::create_table_statement* definition, table_info& info );

	bool write_definition ( run_context& ctx, const sql::create_table_statement* definition, uint32_t root, int64_t& rowid );

	bool mentions_column ( const sql::expression* item, const char* table, const char* column );

}