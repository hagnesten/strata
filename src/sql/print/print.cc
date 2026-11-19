#include "sql/print/print.hh"

namespace sql {

	static bool plain_name ( const char* name ) {

		if ( !name || !*name || ( *name >= '0' && *name <= '9' ) )
			return false;

		for ( const char* at = name; *at; at++ ) {

			char c = *at;

			if ( !( ( c >= 'a' && c <= 'z' ) || ( c >= 'A' && c <= 'Z' ) || ( c >= '0' && c <= '9' ) || c == '_' ) )
				return false;

		}

		static const char* const words [ ] = { "select", "from", "where", "table", "index", "order", "group", "by", "and", "or", "not", "null", "in", "is", "as", "on", "join", "values", "default", "check", "primary", "unique", "key", "limit", "offset", "case", "when", "then", "else", "end", "insert", "update", "delete", "set", "into", "create", "drop", "references" };

		for ( const char* word : words )
			if ( base::same_folded ( name, word ) )
				return false;

		return true;

	}

	void print_name ( base::text& out, const char* name ) {

		if ( plain_name ( name ) ) {

			out.add ( name );

			return;

		}

		out.add ( '"' );

		for ( const char* at = name; at && *at; at++ ) {

			if ( *at == '"' )
				out.add ( '"' );

			out.add ( *at );

		}

		out.add ( '"' );

	}

	void print_value ( base::text& out, const storage::value& item ) {

		static const char digits [ ] = "0123456789ABCDEF";

		switch ( item.kind ) {

			case storage::VALUE_NULL:
				out.add ( "NULL" );
				break;

			case storage::VALUE_INTEGER:
				out.signed_number ( item.integer );
				break;

			case storage::VALUE_REAL:
				out.real ( item.real );
				break;

			case storage::VALUE_TEXT:
				out.add ( '\'' );

				for ( size_t i = 0; i < item.size; i++ ) {

					if ( item.bytes [ i ] == '\'' )
						out.add ( '\'' );

					out.add ( static_cast < char > ( item.bytes [ i ] ) );

				}

				out.add ( '\'' );
				break;

			case storage::VALUE_BLOB:
				out.add ( "X'" );

				for ( size_t i = 0; i < item.size; i++ )
					out.add ( digits [ item.bytes [ i ] >> 4 ] ).add ( digits [ item.bytes [ i ] & 15 ] );

				out.add ( '\'' );
				break;

		}

	}

	static void print_list ( base::text& out, expression* const* list, uint32_t count ) {

		for ( uint32_t i = 0; i < count; i++ ) {

			if ( i )
				out.add ( ", " );

			print_expression ( out, list [ i ] );

		}

	}

	static const char* affinity_name ( affinity_kind kind ) {

		switch ( kind ) {

			case AFFINITY_INTEGER: return "INTEGER";
			case AFFINITY_REAL: return "REAL";
			case AFFINITY_TEXT: return "TEXT";
			case AFFINITY_BLOB: return "BLOB";
			case AFFINITY_NUMERIC: return "NUMERIC";
			default: return "ANY";

		}

	}

	void print_expression ( base::text& out, const expression* item ) {

		if ( !item )
			return;

		switch ( item->kind ) {

			case EXPRESSION_LITERAL:
				print_value ( out, item->literal );
				break;

			case EXPRESSION_PARAMETER:
				if ( item->name )
					out.add ( item->name );
				else
					out.add ( '?' ).number ( item->parameter + 1 );
				break;

			case EXPRESSION_COLUMN:
				if ( item->table ) {

					print_name ( out, item->table );
					out.add ( '.' );

				}

				print_name ( out, item->name );
				break;

			case EXPRESSION_UNARY:
				out.add ( operator_text ( item->op ) );

				if ( item->op == OPERATOR_NOT )
					out.add ( ' ' );

				print_expression ( out, item->left );
				break;

			case EXPRESSION_BINARY:
				out.add ( '(' );
				print_expression ( out, item->left );
				out.add ( ' ' ).add ( operator_text ( item->op ) ).add ( ' ' );
				print_expression ( out, item->right );
				out.add ( ')' );
				break;

			case EXPRESSION_FUNCTION:
				out.add ( item->name ).add ( '(' );

				if ( item->star )
					out.add ( '*' );
				else {

					if ( item->distinct )
						out.add ( "DISTINCT " );

					print_list ( out, item->list, item->count );

				}

				out.add ( ')' );
				break;

			case EXPRESSION_IN:
				print_expression ( out, item->left );
				out.add ( item->negated ? " NOT IN (" : " IN (" );

				if ( item->query )
					print_select ( out, item->query );
				else
					print_list ( out, item->list, item->count );

				out.add ( ')' );
				break;

			case EXPRESSION_BETWEEN:
				print_expression ( out, item->left );
				out.add ( item->negated ? " NOT BETWEEN " : " BETWEEN " );
				print_expression ( out, item->right );
				out.add ( " AND " );
				print_expression ( out, item->third );
				break;

			case EXPRESSION_NULL_TEST:
				print_expression ( out, item->left );
				out.add ( item->negated ? " IS NOT NULL" : " IS NULL" );
				break;

			case EXPRESSION_LIKE:
				print_expression ( out, item->left );
				out.add ( item->negated ? " NOT" : "" ).add ( item->star ? " GLOB " : " LIKE " );
				print_expression ( out, item->right );

				if ( item->third ) {

					out.add ( " ESCAPE " );
					print_expression ( out, item->third );

				}

				break;

			case EXPRESSION_CASE:
				out.add ( "CASE" );

				if ( item->left ) {

					out.add ( ' ' );
					print_expression ( out, item->left );

				}

				for ( uint32_t i = 0; i + 1 < item->count; i += 2 ) {

					out.add ( " WHEN " );
					print_expression ( out, item->list [ i ] );
					out.add ( " THEN " );
					print_expression ( out, item->list [ i + 1 ] );

				}

				if ( item->right ) {

					out.add ( " ELSE " );
					print_expression ( out, item->right );

				}

				out.add ( " END" );
				break;

			case EXPRESSION_CAST:
				out.add ( "CAST(" );
				print_expression ( out, item->left );
				out.add ( " AS " ).add ( item->name ? item->name : affinity_name ( item->cast ) ).add ( ')' );
				break;

			case EXPRESSION_SUBQUERY:
				out.add ( '(' );
				print_select ( out, item->query );
				out.add ( ')' );
				break;

			case EXPRESSION_EXISTS:
				out.add ( "EXISTS (" );
				print_select ( out, item->query );
				out.add ( ')' );
				break;

		}

	}

	static void print_core ( base::text& out, const select_statement* item ) {

		if ( item->values ) {

			out.add ( "VALUES " );

			for ( uint32_t row = 0; row < item->value_rows; row++ ) {

				out.add ( row ? ", (" : "(" );
				print_list ( out, item->values + row * item->value_columns, item->value_columns );
				out.add ( ')' );

			}

			return;

		}

		out.add ( item->distinct ? "SELECT DISTINCT " : "SELECT " );

		for ( uint32_t i = 0; i < item->column_count; i++ ) {

			const result_column& column = item->columns [ i ];

			if ( i )
				out.add ( ", " );

			if ( column.star ) {

				if ( column.star_table ) {

					print_name ( out, column.star_table );
					out.add ( '.' );

				}

				out.add ( '*' );

				continue;

			}

			print_expression ( out, column.value );

			if ( column.alias ) {

				out.add ( " AS " );
				print_name ( out, column.alias );

			}

		}

		for ( uint32_t i = 0; i < item->source_count; i++ ) {

			const table_source& source = item->sources [ i ];

			if ( source.join == JOIN_FIRST )
				out.add ( " FROM " );
			else if ( source.join == JOIN_CROSS )
				out.add ( ", " );
			else {

				if ( source.natural )
					out.add ( " NATURAL" );

				out.add ( source.join == JOIN_LEFT ? " LEFT JOIN " : source.join == JOIN_RIGHT ? " RIGHT JOIN " : source.join == JOIN_FULL ? " FULL JOIN " : " JOIN " );

			}

			if ( source.query ) {

				out.add ( '(' );
				print_select ( out, source.query );
				out.add ( ')' );

			} else
				print_name ( out, source.name );

			if ( source.alias ) {

				out.add ( " AS " );
				print_name ( out, source.alias );

			}

			if ( source.on ) {

				out.add ( " ON " );
				print_expression ( out, source.on );

			}

			if ( source.using_count ) {

				out.add ( " USING (" );

				for ( uint32_t k = 0; k < source.using_count; k++ ) {

					if ( k )
						out.add ( ", " );

					print_name ( out, source.using_names [ k ] );

				}

				out.add ( ')' );

			}

		}

		if ( item->where ) {

			out.add ( " WHERE " );
			print_expression ( out, item->where );

		}

		if ( item->group_count ) {

			out.add ( " GROUP BY " );
			print_list ( out, item->group, item->group_count );

		}

		if ( item->having ) {

			out.add ( " HAVING " );
			print_expression ( out, item->having );

		}

	}

	void print_select ( base::text& out, const select_statement* item ) {

		const select_statement* first = item;

		for ( ; item; item = item->next ) {

			print_core ( out, item );

			switch ( item->compound ) {

				case COMPOUND_UNION: out.add ( " UNION " ); break;
				case COMPOUND_UNION_ALL: out.add ( " UNION ALL " ); break;
				case COMPOUND_INTERSECT: out.add ( " INTERSECT " ); break;
				case COMPOUND_EXCEPT: out.add ( " EXCEPT " ); break;
				default: break;

			}

		}

		for ( uint32_t i = 0; i < first->order_count; i++ ) {

			out.add ( i ? ", " : " ORDER BY " );
			print_expression ( out, first->order [ i ].value );

			if ( first->order [ i ].descending )
				out.add ( " DESC" );

		}

		if ( first->limit ) {

			out.add ( " LIMIT " );
			print_expression ( out, first->limit );

		}

		if ( first->offset ) {

			out.add ( " OFFSET " );
			print_expression ( out, first->offset );

		}

	}

	static const char* action_text ( foreign_action action ) {

		return action == FOREIGN_CASCADE ? " ON DELETE CASCADE" : action == FOREIGN_SET_NULL ? " ON DELETE SET NULL" : "";

	}

	void print_column ( base::text& out, const column_definition& column ) {

		print_name ( out, column.name );

		if ( column.type && *column.type )
			out.add ( ' ' ).add ( column.type );

		if ( column.primary ) {

			out.add ( " PRIMARY KEY" );

			if ( column.descending )
				out.add ( " DESC" );

			if ( column.autoincrement )
				out.add ( " AUTOINCREMENT" );

		}

		if ( column.not_null )
			out.add ( " NOT NULL" );

		if ( column.unique )
			out.add ( " UNIQUE" );

		if ( column.nocase )
			out.add ( " COLLATE NOCASE" );

		if ( column.fallback ) {

			out.add ( " DEFAULT " );

			if ( column.fallback->kind == EXPRESSION_LITERAL )
				print_expression ( out, column.fallback );
			else {

				out.add ( '(' );
				print_expression ( out, column.fallback );
				out.add ( ')' );

			}

		}

		if ( column.check ) {

			out.add ( " CHECK (" );
			print_expression ( out, column.check );
			out.add ( ')' );

		}

		if ( column.references ) {

			out.add ( " REFERENCES " );
			print_name ( out, column.references );

			if ( column.references_column ) {

				out.add ( '(' );
				print_name ( out, column.references_column );
				out.add ( ')' );

			}

			out.add ( action_text ( column.on_delete ) );

		}

	}

	static void print_names ( base::text& out, const char* const* names, uint32_t count ) {

		out.add ( '(' );

		for ( uint32_t i = 0; i < count; i++ ) {

			if ( i )
				out.add ( ", " );

			print_name ( out, names [ i ] );

		}

		out.add ( ')' );

	}

	void print_create_table ( base::text& out, const create_table_statement* item ) {

		out.add ( "CREATE TABLE " );
		print_name ( out, item->name );
		out.add ( " (" );

		for ( uint32_t i = 0; i < item->column_count; i++ ) {

			out.add ( i ? ", " : "" );
			print_column ( out, item->columns [ i ] );

		}

		for ( uint32_t i = 0; i < item->constraint_count; i++ ) {

			const table_constraint& constraint = item->constraints [ i ];

			out.add ( ", " );

			if ( constraint.primary ) {

				out.add ( "PRIMARY KEY " );
				print_names ( out, constraint.columns, constraint.column_count );

			} else if ( constraint.unique ) {

				out.add ( "UNIQUE " );
				print_names ( out, constraint.columns, constraint.column_count );

			} else if ( constraint.check ) {

				out.add ( "CHECK (" );
				print_expression ( out, constraint.check );
				out.add ( ')' );

			} else if ( constraint.references ) {

				out.add ( "FOREIGN KEY " );
				print_names ( out, constraint.columns, constraint.column_count );
				out.add ( " REFERENCES " );
				print_name ( out, constraint.references );

				if ( constraint.references_count ) {

					out.add ( ' ' );
					print_names ( out, constraint.references_columns, constraint.references_count );

				}

				out.add ( action_text ( constraint.on_delete ) );

			}

		}

		out.add ( ')' );

	}

}
