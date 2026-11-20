#pragma once

#include "engine/database/database.hh"

namespace engine {

	struct prepared_statement {

		database* owner;
		char* text;
		size_t text_size;
		base::arena parse;
		base::arena lasting;
		base::arena memory;
		base::arena scratch;
		base::arena results;
		sql::statement* statement;
		table_scope* scopes;
		uint32_t next_id;
		uint64_t loads;
		bool planned;
		bool stale;

	};

	bool prepare_statement ( database& item, const char* sql, prepared_statement*& out, size_t* used );

	bool run_prepared ( prepared_statement& item, const storage::value* parameters, uint32_t parameter_count, query_sink* sink );

	uint32_t statement_parameters ( const prepared_statement& item );

	bool describe_statement ( prepared_statement& item, const char* const*& names, uint32_t& count );

	bool statement_returns_rows ( const prepared_statement& item );

	const prepared_select* statement_plan ( const prepared_statement& item );

	void finish_statement ( prepared_statement* item );

}
