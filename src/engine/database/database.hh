#pragma once

#include "engine/run/run.hh"

namespace engine {

	struct query_sink {

		void* context;
		bool ( *columns ) ( void* context, const char* const* names, uint32_t count );
		bool ( *row ) ( void* context, const storage::value* values, uint32_t count );

	};

	struct savepoint {

		char* name;
		bool opened;

	};

	struct savepoint_stack {

		savepoint* items;
		uint32_t count;
		uint32_t room;

	};

	struct database {

		storage::pager pages;
		catalog* schema;
		bool open;
		bool in_transaction;
		savepoint_stack savepoints;
		int64_t last_rowid;
		int64_t changes;
		int64_t total_changes;
		uint64_t random;
		uint64_t rows_examined;
		uint32_t statements;
		base::text problem;
		bool parse_failed;
		uint32_t error_line;
		uint32_t error_column;

	};

	bool open_database ( database& item, const char* path );

	void close_database ( database& item );

	bool execute ( database& item, const char* sql, const storage::value* parameters, uint32_t parameter_count, query_sink* sink );

	bool reload_schema ( database& item );

	bool pager_problem ( database& item, const char* what );

	bool control_statement ( database& item, run_context& ctx, const sql::statement* statement, bool& handled );

	void forget_savepoints ( database& item );

	void describe_value ( base::text& out, const storage::value& item );

}
