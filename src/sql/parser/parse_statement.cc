#include "sql/parser/parser.hh"

namespace sql {

	static bool parse_names ( parser& item, const char**& out, uint32_t& count ) {

		grow_list < const char* > names;

		if ( !expect_symbol ( item, "(" ) )
			return false;

		do {

			const char* name = take_name ( item, "a column name" );

			if ( !name || !names.push ( item, name ) )
				return false;

			if ( !accept_word ( item, "asc" ) )
				accept_word ( item, "desc" );

			if ( accept_word ( item, "collate" ) && !take_name ( item, "a collation name" ) )
				return false;

		} while ( accept_symbol ( item, "," ) );

		out = names.items;
		count = names.count;

		return expect_symbol ( item, ")" );

	}

	static bool parse_sources ( parser& item, select_statement& out ) {

		grow_list < table_source > sources;

		join_kind next = JOIN_FIRST;

		for ( ;; ) {

			table_source source = { };

			source.join = next;

			if ( accept_symbol ( item, "(" ) ) {

				source.query = parse_select ( item );

				if ( !source.query || !expect_symbol ( item, ")" ) )
					return false;

			} else {

				source.name = take_name ( item, "a table name" );

				if ( !source.name )
					return false;

			}

			if ( accept_word ( item, "as" ) ) {

				source.alias = take_name ( item, "an alias" );

				if ( !source.alias )
					return false;

			} else if ( item.current.kind == TOKEN_NAME && !reserved_word ( item.current ) && !token_is ( item.current, "natural" ) )
				source.alias = take_name ( item, "an alias" );

			if ( source.query && !source.alias )
				return fail_here ( item, "a subquery in FROM needs a name, add AS and a name after it" );

			if ( next != JOIN_FIRST ) {

				if ( accept_word ( item, "on" ) ) {

					source.on = parse_expression ( item );

					if ( !source.on )
						return false;

				} else if ( accept_word ( item, "using" ) ) {

					if ( !parse_names ( item, source.using_names, source.using_count ) )
						return false;

				}

			}

			if ( !sources.push ( item, source ) )
				return false;

			if ( accept_symbol ( item, "," ) ) {

				next = JOIN_CROSS;

				continue;

			}

			if ( accept_word ( item, "left" ) ) {

				accept_word ( item, "outer" );

				if ( !expect_word ( item, "join" ) )
					return false;

				next = JOIN_LEFT;

				continue;

			}

			if ( accept_word ( item, "inner" ) ) {

				if ( !expect_word ( item, "join" ) )
					return false;

				next = JOIN_INNER;

				continue;

			}

			if ( accept_word ( item, "cross" ) ) {

				if ( !expect_word ( item, "join" ) )
					return false;

				next = JOIN_CROSS;

				continue;

			}

			if ( accept_word ( item, "join" ) ) {

				next = JOIN_INNER;

				continue;

			}

			if ( token_is ( item.current, "right" ) || token_is ( item.current, "full" ) || token_is ( item.current, "natural" ) )
				return fail_here ( item, "this join is not supported yet, use LEFT JOIN or JOIN" );

			break;

		}

		out.sources = sources.items;
		out.source_count = sources.count;

		return true;

	}

	static bool parse_values ( parser& item, select_statement& out ) {

		grow_list < expression* > values;
		uint32_t width = 0;

		do {

			token row_start = item.current;

			if ( !expect_symbol ( item, "(" ) )
				return false;

			uint32_t here = 0;

			do {

				expression* one = parse_expression ( item );

				if ( !one || !values.push ( item, one ) )
					return false;

				here++;

			} while ( accept_symbol ( item, "," ) );

			if ( !expect_symbol ( item, ")" ) )
				return false;

			if ( width && here != width ) {

				item.current = row_start;

				return fail_here ( item, "every row in VALUES needs the same number of values" );

			}

			width = here;
			out.value_rows++;

		} while ( accept_symbol ( item, "," ) );

		out.values = values.items;
		out.value_columns = width;

		return true;

	}

	static select_statement* parse_core ( parser& item ) {

		select_statement* out = item.memory->make < select_statement > ( 1 );

		if ( !out ) {

			fail_here ( item, "out of memory" );

			return nullptr;

		}

		if ( accept_word ( item, "values" ) )
			return parse_values ( item, *out ) ? out : nullptr;

		if ( !expect_word ( item, "select" ) )
			return nullptr;

		out->distinct = accept_word ( item, "distinct" );

		if ( !out->distinct )
			accept_word ( item, "all" );

		grow_list < result_column > columns;

		do {

			result_column column = { };

			if ( accept_symbol ( item, "*" ) )
				column.star = true;
			else {

				lexer saved = item.lex;
				token here = item.current;

				if ( here.kind == TOKEN_NAME && !reserved_word ( here ) ) {

					const char* name = take_name ( item, "a name" );

					if ( name && accept_symbol ( item, "." ) && accept_symbol ( item, "*" ) ) {

						column.star = true;
						column.star_table = name;

					} else {

						item.lex = saved;
						item.current = here;

					}

				}

				if ( !column.star ) {

					const char* written = item.current.start;

					column.value = parse_expression ( item );

					if ( !column.value )
						return nullptr;

					const char* end = item.current.start;

					while ( end > written && ( end [ -1 ] == ' ' || end [ -1 ] == '\t' || end [ -1 ] == '\n' || end [ -1 ] == '\r' ) )
						end--;

					column.written = item.memory->keep ( written, static_cast < size_t > ( end - written ) );

					if ( accept_word ( item, "as" ) ) {

						column.alias = item.current.kind == TOKEN_STRING ? nullptr : take_name ( item, "an alias" );

						if ( !column.alias && item.current.kind == TOKEN_STRING ) {

							column.alias = item.memory->keep ( item.current.start + 1, item.current.size - 2 );
							advance ( item );

						}

						if ( !column.alias )
							return nullptr;

					} else if ( item.current.kind == TOKEN_NAME && !reserved_word ( item.current ) )
						column.alias = take_name ( item, "an alias" );

				}

			}

			if ( !columns.push ( item, column ) )
				return nullptr;

		} while ( accept_symbol ( item, "," ) );

		out->columns = columns.items;
		out->column_count = columns.count;

		if ( accept_word ( item, "from" ) && !parse_sources ( item, *out ) )
			return nullptr;

		if ( accept_word ( item, "where" ) ) {

			out->where = parse_expression ( item );

			if ( !out->where )
				return nullptr;

		}

		if ( accept_word ( item, "group" ) ) {

			grow_list < expression* > group;

			if ( !expect_word ( item, "by" ) )
				return nullptr;

			do {

				expression* one = parse_expression ( item );

				if ( !one || !group.push ( item, one ) )
					return nullptr;

			} while ( accept_symbol ( item, "," ) );

			out->group = group.items;
			out->group_count = group.count;

			if ( accept_word ( item, "having" ) ) {

				out->having = parse_expression ( item );

				if ( !out->having )
					return nullptr;

			}

		} else if ( accept_word ( item, "having" ) ) {

			out->having = parse_expression ( item );

			if ( !out->having )
				return nullptr;

		}

		return out;

	}

	select_statement* parse_select ( parser& item ) {

		if ( ++item.depth > PARSE_DEPTH ) {

			fail_here ( item, "this query is nested too deeply" );

			return nullptr;

		}

		grow_list < common_table > tables;
		bool recursive = false;

		if ( accept_word ( item, "with" ) ) {

			recursive = accept_word ( item, "recursive" );

			do {

				common_table table = { };

				table.name = take_name ( item, "a name for the WITH query" );

				if ( !table.name || ( symbol_is ( item.current, "(" ) && !parse_names ( item, table.columns, table.column_count ) ) || !expect_word ( item, "as" ) ) {

					item.depth--;

					return nullptr;

				}

				accept_word ( item, "materialized" );

				if ( !expect_symbol ( item, "(" ) ) {

					item.depth--;

					return nullptr;

				}

				table.query = parse_select ( item );

				if ( !table.query || !expect_symbol ( item, ")" ) || !tables.push ( item, table ) ) {

					item.depth--;

					return nullptr;

				}

			} while ( accept_symbol ( item, "," ) );

		}

		select_statement* first = parse_core ( item );
		select_statement* last = first;

		if ( first ) {

			first->with = tables.items;
			first->with_count = tables.count;
			first->recursive = recursive;

		}

		while ( last ) {

			compound_kind kind = COMPOUND_NONE;

			if ( accept_word ( item, "union" ) )
				kind = accept_word ( item, "all" ) ? COMPOUND_UNION_ALL : COMPOUND_UNION;
			else if ( accept_word ( item, "intersect" ) )
				kind = COMPOUND_INTERSECT;
			else if ( accept_word ( item, "except" ) )
				kind = COMPOUND_EXCEPT;
			else
				break;

			last->compound = kind;
			last->next = parse_core ( item );
			last = last->next;

		}

		if ( !last ) {

			item.depth--;

			return nullptr;

		}

		if ( accept_word ( item, "order" ) ) {

			grow_list < order_term > order;

			if ( !expect_word ( item, "by" ) ) {

				item.depth--;

				return nullptr;

			}

			do {

				order_term term = { };

				term.value = parse_expression ( item );

				if ( !term.value ) {

					item.depth--;

					return nullptr;

				}

				if ( accept_word ( item, "desc" ) )
					term.descending = true;
				else
					accept_word ( item, "asc" );

				if ( !order.push ( item, term ) ) {

					item.depth--;

					return nullptr;

				}

			} while ( accept_symbol ( item, "," ) );

			first->order = order.items;
			first->order_count = order.count;

		}

		if ( accept_word ( item, "limit" ) ) {

			first->limit = parse_expression ( item );

			if ( first->limit && accept_symbol ( item, "," ) ) {

				first->offset = first->limit;
				first->limit = parse_expression ( item );

			} else if ( first->limit && accept_word ( item, "offset" ) )
				first->offset = parse_expression ( item );

			if ( !first->limit || item.error->failed ) {

				item.depth--;

				return nullptr;

			}

		}

		item.depth--;

		return item.error->failed ? nullptr : first;

	}

	static bool parse_foreign_action ( parser& item, foreign_action& out ) {

		while ( accept_word ( item, "on" ) ) {

			bool deleting = accept_word ( item, "delete" );

			if ( !deleting && !expect_word ( item, "update" ) )
				return false;

			foreign_action action = FOREIGN_RESTRICT;

			if ( accept_word ( item, "cascade" ) )
				action = FOREIGN_CASCADE;
			else if ( accept_word ( item, "set" ) ) {

				if ( !accept_word ( item, "null" ) && !expect_word ( item, "default" ) )
					return false;

				action = FOREIGN_SET_NULL;

			} else if ( accept_word ( item, "no" ) ) {

				if ( !expect_word ( item, "action" ) )
					return false;

			} else if ( !expect_word ( item, "restrict" ) )
				return false;

			if ( deleting )
				out = action;

		}

		return true;

	}

	static bool parse_column ( parser& item, column_definition& out ) {

		out = { };
		out.name = take_name ( item, "a column name" );

		if ( !out.name )
			return false;

		base::text type;

		while ( item.current.kind == TOKEN_NAME && !reserved_word ( item.current ) && !token_is ( item.current, "not" ) && !token_is ( item.current, "constraint" ) && !token_is ( item.current, "generated" ) ) {

			if ( type.size )
				type.add ( ' ' );

			type.add ( item.current.start, item.current.size );
			advance ( item );

		}

		if ( type.size && accept_symbol ( item, "(" ) ) {

			type.add ( '(' );

			while ( !symbol_is ( item.current, ")" ) && item.current.kind != TOKEN_END ) {

				type.add ( item.current.start, item.current.size );
				advance ( item );

			}

			type.add ( ')' );

			if ( !expect_symbol ( item, ")" ) )
				return false;

		}

		out.type = item.memory->keep ( type.value ( ), type.size );
		out.affinity = affinity_of ( type.value ( ) );

		for ( ;; ) {

			if ( accept_word ( item, "constraint" ) ) {

				if ( !take_name ( item, "a constraint name" ) )
					return false;

				continue;

			}

			if ( accept_word ( item, "primary" ) ) {

				if ( !expect_word ( item, "key" ) )
					return false;

				out.primary = true;
				out.descending = accept_word ( item, "desc" );

				if ( !out.descending )
					accept_word ( item, "asc" );

				out.autoincrement = accept_word ( item, "autoincrement" );

				continue;

			}

			if ( accept_word ( item, "not" ) ) {

				if ( !expect_word ( item, "null" ) )
					return false;

				out.not_null = true;

				continue;

			}

			if ( accept_word ( item, "null" ) )
				continue;

			if ( accept_word ( item, "unique" ) ) {

				out.unique = true;

				continue;

			}

			if ( accept_word ( item, "default" ) ) {

				const char* start = item.current.start;

				if ( accept_symbol ( item, "(" ) ) {

					out.fallback = parse_expression ( item );

					if ( !out.fallback || !expect_symbol ( item, ")" ) )
						return false;

				} else if ( symbol_is ( item.current, "-" ) || symbol_is ( item.current, "+" ) ) {

					out.fallback = parse_expression ( item );

					if ( !out.fallback )
						return false;

				} else {

					token here = item.current;

					if ( here.kind != TOKEN_INTEGER && here.kind != TOKEN_REAL && here.kind != TOKEN_STRING && here.kind != TOKEN_BLOB && !token_is ( here, "null" ) && !token_is ( here, "true" ) && !token_is ( here, "false" ) )
						return fail_here ( item, "a DEFAULT needs a constant or an expression in brackets" );

					out.fallback = parse_expression ( item );

					if ( !out.fallback )
						return false;

				}

				out.fallback_text = item.memory->keep ( start, static_cast < size_t > ( item.current.start - start ) );

				continue;

			}

			if ( accept_word ( item, "check" ) ) {

				if ( !expect_symbol ( item, "(" ) )
					return false;

				out.check = parse_expression ( item );

				if ( !out.check || !expect_symbol ( item, ")" ) )
					return false;

				continue;

			}

			if ( accept_word ( item, "references" ) ) {

				out.references = take_name ( item, "a table name" );

				if ( !out.references )
					return false;

				if ( accept_symbol ( item, "(" ) ) {

					out.references_column = take_name ( item, "a column name" );

					if ( !out.references_column || !expect_symbol ( item, ")" ) )
						return false;

				}

				if ( !parse_foreign_action ( item, out.on_delete ) )
					return false;

				continue;

			}

			if ( accept_word ( item, "collate" ) ) {

				if ( !take_name ( item, "a collation name" ) )
					return false;

				continue;

			}

			break;

		}

		return true;

	}

	static bool parse_constraint ( parser& item, table_constraint& out ) {

		out = { };

		if ( accept_word ( item, "constraint" ) && !take_name ( item, "a constraint name" ) )
			return false;

		if ( accept_word ( item, "primary" ) ) {

			out.primary = true;

			return expect_word ( item, "key" ) && parse_names ( item, out.columns, out.column_count );

		}

		if ( accept_word ( item, "unique" ) ) {

			out.unique = true;

			return parse_names ( item, out.columns, out.column_count );

		}

		if ( accept_word ( item, "check" ) ) {

			if ( !expect_symbol ( item, "(" ) )
				return false;

			out.check = parse_expression ( item );

			return out.check && expect_symbol ( item, ")" );

		}

		if ( accept_word ( item, "foreign" ) ) {

			if ( !expect_word ( item, "key" ) || !parse_names ( item, out.columns, out.column_count ) || !expect_word ( item, "references" ) )
				return false;

			out.references = take_name ( item, "a table name" );

			if ( !out.references )
				return false;

			if ( symbol_is ( item.current, "(" ) && !parse_names ( item, out.references_columns, out.references_count ) )
				return false;

			return parse_foreign_action ( item, out.on_delete );

		}

		return fail_here ( item, "expected a column or a table constraint" );

	}

	static bool if_exists ( parser& item ) {

		if ( !accept_word ( item, "if" ) )
			return false;

		return expect_word ( item, "exists" );

	}

	static bool parse_create ( parser& item, statement& out ) {

		bool unique = accept_word ( item, "unique" );

		if ( accept_word ( item, "index" ) ) {

			create_index_statement* made = item.memory->make < create_index_statement > ( 1 );

			if ( !made )
				return fail_here ( item, "out of memory" );

			out.kind = STATEMENT_CREATE_INDEX;
			out.create_index = made;
			made->unique = unique;

			if ( accept_word ( item, "if" ) ) {

				if ( !expect_word ( item, "not" ) || !expect_word ( item, "exists" ) )
					return false;

				made->if_not_exists = true;

			}

			made->name = take_name ( item, "an index name" );

			if ( !made->name || !expect_word ( item, "on" ) )
				return false;

			made->table = take_name ( item, "a table name" );

			return made->table && parse_names ( item, made->columns, made->column_count );

		}

		if ( unique )
			return fail_here ( item, "expected INDEX after UNIQUE" );

		accept_word ( item, "temp" ) || accept_word ( item, "temporary" );

		if ( !expect_word ( item, "table" ) )
			return false;

		create_table_statement* made = item.memory->make < create_table_statement > ( 1 );

		if ( !made )
			return fail_here ( item, "out of memory" );

		out.kind = STATEMENT_CREATE_TABLE;
		out.create_table = made;

		if ( accept_word ( item, "if" ) ) {

			if ( !expect_word ( item, "not" ) || !expect_word ( item, "exists" ) )
				return false;

			made->if_not_exists = true;

		}

		made->name = take_name ( item, "a table name" );

		if ( !made->name )
			return false;

		if ( accept_word ( item, "as" ) ) {

			made->query = parse_select ( item );

			return made->query != nullptr;

		}

		if ( !expect_symbol ( item, "(" ) )
			return false;

		grow_list < column_definition > columns;
		grow_list < table_constraint > constraints;

		do {

			if ( token_is ( item.current, "primary" ) || token_is ( item.current, "unique" ) || token_is ( item.current, "check" ) || token_is ( item.current, "foreign" ) || token_is ( item.current, "constraint" ) ) {

				table_constraint constraint;

				if ( !parse_constraint ( item, constraint ) || !constraints.push ( item, constraint ) )
					return false;

				continue;

			}

			if ( constraints.count )
				return fail_here ( item, "columns must come before table constraints" );

			column_definition column;

			if ( !parse_column ( item, column ) || !columns.push ( item, column ) )
				return false;

		} while ( accept_symbol ( item, "," ) );

		if ( !expect_symbol ( item, ")" ) )
			return false;

		accept_word ( item, "strict" );

		made->columns = columns.items;
		made->column_count = columns.count;
		made->constraints = constraints.items;
		made->constraint_count = constraints.count;

		if ( !made->column_count )
			return fail_here ( item, "a table needs at least one column" );

		return true;

	}

	static conflict_kind parse_or_conflict ( parser& item ) {

		if ( !accept_word ( item, "or" ) )
			return CONFLICT_ABORT;

		if ( accept_word ( item, "replace" ) )
			return CONFLICT_REPLACE;

		if ( accept_word ( item, "ignore" ) )
			return CONFLICT_IGNORE;

		if ( accept_word ( item, "abort" ) || accept_word ( item, "fail" ) || accept_word ( item, "rollback" ) )
			return CONFLICT_ABORT;

		fail_here ( item, "expected REPLACE, IGNORE or ABORT" );

		return CONFLICT_ABORT;

	}

	static bool parse_assignments ( parser& item, assignment*& out, uint32_t& count ) {

		grow_list < assignment > list;

		do {

			assignment one = { };

			one.column = take_name ( item, "a column name" );

			if ( !one.column || !expect_symbol ( item, "=" ) )
				return false;

			one.value = parse_expression ( item );

			if ( !one.value || !list.push ( item, one ) )
				return false;

		} while ( accept_symbol ( item, "," ) );

		out = list.items;
		count = list.count;

		return true;

	}

	static bool parse_insert ( parser& item, statement& out, bool replace ) {

		insert_statement* made = item.memory->make < insert_statement > ( 1 );

		if ( !made )
			return fail_here ( item, "out of memory" );

		out.kind = STATEMENT_INSERT;
		out.insert = made;
		made->conflict = replace ? CONFLICT_REPLACE : parse_or_conflict ( item );

		if ( item.error->failed || !expect_word ( item, "into" ) )
			return false;

		made->table = take_name ( item, "a table name" );

		if ( !made->table )
			return false;

		if ( symbol_is ( item.current, "(" ) && !parse_names ( item, made->columns, made->column_count ) )
			return false;

		if ( accept_word ( item, "default" ) ) {

			if ( !expect_word ( item, "values" ) )
				return false;

		} else {

			made->query = parse_select ( item );

			if ( !made->query )
				return false;

		}

		if ( accept_word ( item, "on" ) ) {

			if ( !expect_word ( item, "conflict" ) )
				return false;

			if ( symbol_is ( item.current, "(" ) && !parse_names ( item, made->conflict_columns, made->conflict_count ) )
				return false;

			if ( !expect_word ( item, "do" ) )
				return false;

			if ( accept_word ( item, "nothing" ) )
				made->conflict = CONFLICT_NOTHING;
			else {

				if ( !expect_word ( item, "update" ) || !expect_word ( item, "set" ) || !parse_assignments ( item, made->updates, made->update_count ) )
					return false;

				made->conflict = CONFLICT_UPDATE;

				if ( accept_word ( item, "where" ) ) {

					made->update_where = parse_expression ( item );

					if ( !made->update_where )
						return false;

				}

			}

		}

		return true;

	}

	static bool parse_body ( parser& item, statement& out ) {

		if ( token_is ( item.current, "select" ) || token_is ( item.current, "values" ) || token_is ( item.current, "with" ) ) {

			out.kind = STATEMENT_SELECT;
			out.select = parse_select ( item );

			return out.select != nullptr;

		}

		if ( accept_word ( item, "insert" ) )
			return parse_insert ( item, out, false );

		if ( accept_word ( item, "replace" ) )
			return parse_insert ( item, out, true );

		if ( accept_word ( item, "update" ) ) {

			update_statement* made = item.memory->make < update_statement > ( 1 );

			if ( !made )
				return fail_here ( item, "out of memory" );

			out.kind = STATEMENT_UPDATE;
			out.update = made;
			made->conflict = parse_or_conflict ( item );
			made->table = item.error->failed ? nullptr : take_name ( item, "a table name" );

			if ( !made->table || !expect_word ( item, "set" ) || !parse_assignments ( item, made->assignments, made->assignment_count ) )
				return false;

			if ( accept_word ( item, "where" ) ) {

				made->where = parse_expression ( item );

				return made->where != nullptr;

			}

			return true;

		}

		if ( accept_word ( item, "delete" ) ) {

			delete_statement* made = item.memory->make < delete_statement > ( 1 );

			if ( !made )
				return fail_here ( item, "out of memory" );

			out.kind = STATEMENT_DELETE;
			out.remove = made;

			if ( !expect_word ( item, "from" ) )
				return false;

			made->table = take_name ( item, "a table name" );

			if ( !made->table )
				return false;

			if ( accept_word ( item, "where" ) ) {

				made->where = parse_expression ( item );

				return made->where != nullptr;

			}

			return true;

		}

		if ( accept_word ( item, "create" ) )
			return parse_create ( item, out );

		if ( accept_word ( item, "drop" ) ) {

			bool index = accept_word ( item, "index" );

			if ( !index && !expect_word ( item, "table" ) )
				return false;

			drop_statement* made = item.memory->make < drop_statement > ( 1 );

			if ( !made )
				return fail_here ( item, "out of memory" );

			out.kind = index ? STATEMENT_DROP_INDEX : STATEMENT_DROP_TABLE;
			out.drop = made;
			made->if_exists = if_exists ( item );
			made->name = item.error->failed ? nullptr : take_name ( item, index ? "an index name" : "a table name" );

			return made->name != nullptr;

		}

		if ( accept_word ( item, "alter" ) ) {

			alter_statement* made = item.memory->make < alter_statement > ( 1 );

			if ( !made || !expect_word ( item, "table" ) )
				return made ? false : fail_here ( item, "out of memory" );

			out.kind = STATEMENT_ALTER_TABLE;
			out.alter = made;
			made->table = take_name ( item, "a table name" );

			if ( !made->table )
				return false;

			if ( accept_word ( item, "add" ) ) {

				accept_word ( item, "column" );
				made->kind = ALTER_ADD_COLUMN;
				made->column = item.memory->make < column_definition > ( 1 );

				return made->column && parse_column ( item, *made->column );

			}

			if ( !expect_word ( item, "rename" ) )
				return false;

			if ( accept_word ( item, "to" ) ) {

				made->kind = ALTER_RENAME_TABLE;
				made->new_name = take_name ( item, "a table name" );

				return made->new_name != nullptr;

			}

			accept_word ( item, "column" );
			made->kind = ALTER_RENAME_COLUMN;
			made->old_name = take_name ( item, "a column name" );

			if ( !made->old_name || !expect_word ( item, "to" ) )
				return false;

			made->new_name = take_name ( item, "a column name" );

			return made->new_name != nullptr;

		}

		if ( accept_word ( item, "begin" ) ) {

			out.kind = STATEMENT_BEGIN;
			accept_word ( item, "deferred" ) || accept_word ( item, "immediate" ) || accept_word ( item, "exclusive" );
			accept_word ( item, "transaction" );

			return true;

		}

		if ( accept_word ( item, "commit" ) || accept_word ( item, "end" ) ) {

			out.kind = STATEMENT_COMMIT;
			accept_word ( item, "transaction" );

			return true;

		}

		if ( accept_word ( item, "rollback" ) ) {

			out.kind = STATEMENT_ROLLBACK;
			accept_word ( item, "transaction" );

			if ( token_is ( item.current, "to" ) )
				return fail_here ( item, "savepoints are not supported yet" );

			return true;

		}

		if ( token_is ( item.current, "savepoint" ) || token_is ( item.current, "release" ) )
			return fail_here ( item, "savepoints are not supported yet" );

		if ( accept_word ( item, "analyze" ) ) {

			out.kind = STATEMENT_ANALYZE;

			if ( item.current.kind == TOKEN_NAME )
				take_name ( item, "a table name" );

			return true;

		}

		if ( accept_word ( item, "vacuum" ) ) {

			out.kind = STATEMENT_VACUUM;

			return true;

		}

		return fail_here ( item, "expected a statement like SELECT, INSERT, UPDATE, DELETE or CREATE" );

	}

	bool parse_one ( base::arena& memory, const char* source, size_t& consumed, statement*& out, parse_error& error ) {

		parser item = { };

		item.memory = &memory;
		item.error = &error;
		error.failed = false;
		error.message.clear ( );
		start_lexer ( item.lex, source );
		advance ( item );
		out = nullptr;

		while ( accept_symbol ( item, ";" ) ) {

		}

		if ( item.current.kind == TOKEN_END ) {

			consumed = static_cast < size_t > ( item.current.start - source );

			return true;

		}

		statement* made = memory.make < statement > ( 1 );

		if ( !made )
			return fail_here ( item, "out of memory" );

		const char* start = item.current.start;
		bool fine = true;

		if ( accept_word ( item, "explain" ) ) {

			made->kind = STATEMENT_EXPLAIN;

			if ( accept_word ( item, "query" ) )
				fine = expect_word ( item, "plan" );

			made->query_plan = true;
			made->inner = memory.make < statement > ( 1 );
			fine = fine && made->inner && parse_body ( item, *made->inner );

		} else
			fine = parse_body ( item, *made );

		if ( fine && !symbol_is ( item.current, ";" ) && item.current.kind != TOKEN_END )
			fine = fail_here ( item, "expected the end of the statement" );

		if ( !fine )
			return false;

		made->text = memory.keep ( start, static_cast < size_t > ( item.current.start - start ) );
		made->text_size = static_cast < uint32_t > ( item.current.start - start );
		made->parameter_count = item.parameters;

		if ( made->inner ) {

			made->inner->text = made->text;
			made->inner->text_size = made->text_size;
			made->inner->parameter_count = item.parameters;

		}

		accept_symbol ( item, ";" );
		consumed = static_cast < size_t > ( item.current.start - source );
		out = made;

		return true;

	}

}
