#pragma once

#include "sql/syntax/syntax.hh"

namespace sql {

	void print_name ( base::text& out, const char* name );

	void print_value ( base::text& out, const storage::value& item );

	void print_expression ( base::text& out, const expression* item );

	void print_select ( base::text& out, const select_statement* item );

	void print_create_table ( base::text& out, const create_table_statement* item );

	void print_column ( base::text& out, const column_definition& column );

}
