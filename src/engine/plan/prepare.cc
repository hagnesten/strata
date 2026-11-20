#include "engine/search/search.hh"
#include "sql/print/print.hh"

namespace engine {

	bool fail_run ( run_context& ctx, const char* message ) {

		if ( !ctx.problem->size )
			ctx.problem->add ( message );

		return false;

	}

	bool fail_named ( run_context& ctx, const char* first, const char* name, const char* rest ) {

		if ( !ctx.problem->size )
			ctx.problem->add ( first ).add ( name ? name : "" ).add ( rest ? rest : "" );

		return false;

	}

	bool begin_writes ( run_context& ctx, bool risky ) {

		if ( !ctx.mark_pending )
			return true;

		ctx.mark_pending = false;

		if ( !risky ) {

			ctx.unmarked_writes = true;

			return true;

		}

		if ( !storage::begin_statement ( *ctx.pages ) )
			return fail_storage ( ctx, "the statement could not start" );

		ctx.marked = true;

		return true;

	}

	bool fail_storage ( run_context& ctx, const char* what ) {

		if ( !ctx.problem->size ) {

			ctx.problem->add ( what );

			if ( ctx.pages->problem [ 0 ] )
				ctx.problem->add ( ": " ).add ( ctx.pages->problem );

		}

		return false;

	}

	static int32_t highest_position ( const prepared_select* plan, const sql::expression* item ) {

		if ( !item )
			return -1;

		int32_t highest = -1;

		if ( item->kind == sql::EXPRESSION_COLUMN && item->depth == 0 && item->source >= 0 && static_cast < uint32_t > ( item->source ) < plan->source_count )
			highest = static_cast < int32_t > ( plan->sources [ item->source ].position );

		highest = base::max ( highest, highest_position ( plan, item->left ) );
		highest = base::max ( highest, highest_position ( plan, item->right ) );
		highest = base::max ( highest, highest_position ( plan, item->third ) );

		for ( uint32_t i = 0; i < item->count; i++ )
			highest = base::max ( highest, highest_position ( plan, item->list [ i ] ) );

		return highest;

	}

	static void note_use ( prepared_source& source, int32_t column ) {

		if ( source.used && column >= 0 && static_cast < uint32_t > ( column ) < source.width )
			source.used [ column ] = true;

	}

	static sql::expression* bound_column ( base::arena& memory, uint32_t source, int32_t column, sql::affinity_kind affinity, const char* name, const char* table ) {

		sql::expression* made = memory.make < sql::expression > ( 1 );

		if ( !made )
			return nullptr;

		made->kind = sql::EXPRESSION_COLUMN;
		made->source = static_cast < int32_t > ( source );
		made->column = column;
		made->aggregate = -1;
		made->cast = affinity;
		made->name = name;
		made->table = table;

		return made;

	}

	sql::affinity_kind expression_affinity ( const sql::expression* item ) {

		if ( item && ( item->kind == sql::EXPRESSION_COLUMN || item->kind == sql::EXPRESSION_CAST ) )
			return item->cast;

		return sql::AFFINITY_ANY;

	}

	bool split_terms ( run_context& ctx, sql::expression* item, sql::expression**& out, uint32_t& count, uint32_t& room ) {

		if ( !item )
			return true;

		if ( item->kind == sql::EXPRESSION_BINARY && item->op == sql::OPERATOR_AND )
			return split_terms ( ctx, item->left, out, count, room ) && split_terms ( ctx, item->right, out, count, room );

		if ( !base::push ( *ctx.lasting, out, count, room, item ) )
			return fail_run ( ctx, "out of memory" );

		return true;

	}

	int32_t highest_source ( const sql::expression* item, uint32_t nest ) {

		if ( !item )
			return -1;

		if ( item->kind == sql::EXPRESSION_COLUMN )
			return item->depth == nest ? item->source : -1;

		int32_t best = -1;

		if ( item->query ) {

			const prepared_select* child = static_cast < const prepared_select* > ( item->query->prepared );

			if ( !child || child->correlated )
				best = 0x7FFFFFFF;

		}

		best = base::max ( best, highest_source ( item->left, nest ) );
		best = base::max ( best, highest_source ( item->right, nest ) );
		best = base::max ( best, highest_source ( item->third, nest ) );

		for ( uint32_t i = 0; i < item->count; i++ )
			best = base::max ( best, highest_source ( item->list [ i ], nest ) );

		return best;

	}

	static bool contains_aggregate ( const sql::expression* item ) {

		if ( !item )
			return false;

		if ( item->kind == sql::EXPRESSION_FUNCTION && item->aggregate >= 0 )
			return true;

		if ( contains_aggregate ( item->left ) || contains_aggregate ( item->right ) || contains_aggregate ( item->third ) )
			return true;

		for ( uint32_t i = 0; i < item->count; i++ )
			if ( contains_aggregate ( item->list [ i ] ) )
				return true;

		return false;

	}

	static bool add_child ( run_context& ctx, prepared_select* scope, prepared_select* plan, child_role role ) {

		plan_child* made = ctx.lasting->make < plan_child > ( 1 );

		if ( !made )
			return fail_run ( ctx, "out of memory" );

		made->role = role;
		made->plan = plan;

		plan_child** tail = &scope->children;

		while ( *tail )
			tail = &( *tail )->next;

		*tail = made;

		return true;

	}

	static bool is_rowid_name ( const char* name ) {

		return base::same_folded ( name, "rowid" ) || base::same_folded ( name, "oid" ) || base::same_folded ( name, "_rowid_" );

	}

	static bool resolve_column ( run_context& ctx, prepared_select* scope, sql::expression* item, bool& found ) {

		found = false;

		uint32_t depth = 0;

		for ( prepared_select* query = scope; query; query = query->parent, depth++ ) {

			int32_t hit_source = -1;
			int32_t hit_column = -1;
			int32_t hidden_source = -1;
			int32_t hidden_column = -1;
			uint32_t hits = 0;
			bool table_seen = false;

			for ( uint32_t s = 0; s < query->source_count; s++ ) {

				prepared_source& source = query->sources [ s ];

				if ( item->table && !base::same_folded ( item->table, source.alias ) )
					continue;

				table_seen = true;

				for ( uint32_t c = 0; c < source.width; c++ ) {

					if ( !base::same_folded ( source.names [ c ], item->name ) )
						continue;

					if ( !item->table && source.hidden [ c ] ) {

						if ( hidden_source < 0 ) {

							hidden_source = static_cast < int32_t > ( s );
							hidden_column = static_cast < int32_t > ( c );

						}

						break;

					}

					hits++;
					hit_source = static_cast < int32_t > ( s );
					hit_column = static_cast < int32_t > ( c );

					break;

				}

			}

			if ( !hits && hidden_source >= 0 ) {

				hits = 1;
				hit_source = hidden_source;
				hit_column = hidden_column;

			}

			if ( !hits && is_rowid_name ( item->name ) ) {

				for ( uint32_t s = 0; s < query->source_count; s++ ) {

					prepared_source& source = query->sources [ s ];

					if ( !source.table || ( item->table && !base::same_folded ( item->table, source.alias ) ) )
						continue;

					hits++;
					hit_source = static_cast < int32_t > ( s );
					hit_column = source.table->rowid_column;

				}

			}

			if ( hits > 1 )
				return fail_named ( ctx, "the column name ", item->name, " is ambiguous, put a table name and a dot before it" );

			if ( hits == 1 ) {

				prepared_source& source = query->sources [ hit_source ];

				item->depth = depth;
				item->source = hit_source;
				item->column = hit_column;
				item->cast = hit_column >= 0 ? source.affinities [ hit_column ] : sql::AFFINITY_INTEGER;
				note_use ( source, hit_column );

				if ( hit_column >= 0 && source.paired_source && source.paired_source [ hit_column ] >= 0 )
					note_use ( query->sources [ source.paired_source [ hit_column ] ], source.paired_column [ hit_column ] );

				if ( hit_column >= 0 && source.nocase && source.nocase [ hit_column ] )
					item->nocase = true;

				found = true;

				for ( prepared_select* inner = scope; inner != query; inner = inner->parent )
					inner->correlated = true;

				return true;

			}

			if ( item->table && table_seen )
				return fail_named ( ctx, "no such column: ", item->name, "" );

		}

		return true;

	}

	static bool bind ( run_context& ctx, prepared_select* scope, sql::expression* item, bind_mode mode, bool aliases );

	static bool bind_children ( run_context& ctx, prepared_select* scope, sql::expression* item, bind_mode mode, bool aliases ) {

		if ( !bind ( ctx, scope, item->left, mode, aliases ) || !bind ( ctx, scope, item->right, mode, aliases ) || !bind ( ctx, scope, item->third, mode, aliases ) )
			return false;

		for ( uint32_t i = 0; i < item->count; i++ )
			if ( !bind ( ctx, scope, item->list [ i ], mode, aliases ) )
				return false;

		return true;

	}

	static bool resolve_row ( prepared_select* scope, sql::expression* item ) {

		if ( !item || item->kind != sql::EXPRESSION_COLUMN || item->table || !item->name )
			return false;

		uint32_t depth = 0;

		for ( prepared_select* query = scope; query; query = query->parent, depth++ ) {

			for ( uint32_t s = 0; s < query->source_count; s++ ) {

				prepared_source& source = query->sources [ s ];

				if ( !source.table || !base::same_folded ( source.alias, item->name ) )
					continue;

				item->depth = depth;
				item->source = static_cast < int32_t > ( s );
				item->column = -1;
				item->cast = sql::AFFINITY_INTEGER;

				for ( prepared_select* inner = scope; inner != query; inner = inner->parent )
					inner->correlated = true;

				return true;

			}

		}

		return false;

	}

	static prepared_source* bound_source ( prepared_select* scope, const sql::expression* item ) {

		prepared_select* query = scope;

		for ( uint32_t i = 0; i < item->depth && query; i++ )
			query = query->parent;

		return query && item->source >= 0 ? &query->sources [ item->source ] : nullptr;

	}

	static bool bind_match ( run_context& ctx, prepared_select* scope, sql::expression* item, bind_mode mode, bool aliases ) {

		if ( mode == BIND_CONSTANT )
			return fail_run ( ctx, "MATCH cannot be used here" );

		sql::expression* target = item->left;

		if ( !target || target->kind != sql::EXPRESSION_COLUMN )
			return fail_run ( ctx, "MATCH needs a table or one of its columns on its left" );

		bool found = target->source >= 0;
		bool whole = target->source >= 0 && target->column < 0;

		if ( !found && !resolve_column ( ctx, scope, target, found ) )
			return false;

		if ( !found ) {

			if ( !resolve_row ( scope, target ) )
				return fail_named ( ctx, "no such table or column for MATCH: ", target->name, "" );

			whole = true;

		}

		prepared_source* source = bound_source ( scope, target );

		if ( !source || !source->table )
			return fail_run ( ctx, "MATCH needs a table with a FULLTEXT index on its left" );

		search_binding* binding = ctx.lasting->make < search_binding > ( 1 );

		if ( !binding )
			return fail_run ( ctx, "out of memory" );

		for ( uint32_t i = 0; i < ctx.schema->index_count && !binding->index; i++ ) {

			index_info& index = ctx.schema->indexes [ i ];

			if ( !index.fulltext || !base::same_folded ( index.table, source->table->name ) )
				continue;

			if ( whole ) {

				binding->index = &index;
				binding->columns = ~0ull;

				continue;

			}

			for ( uint32_t k = 0; k < index.column_count; k++ ) {

				if ( static_cast < int32_t > ( index.columns [ k ] ) == target->column ) {

					binding->index = &index;
					binding->columns = 1ull << k;

				}

			}

		}

		if ( !binding->index )
			return fail_named ( ctx, "table ", source->table->name, whole ? " has no FULLTEXT index for MATCH" : " has no FULLTEXT index on that column" );

		binding->table = source->table;
		binding->source = target->source;
		binding->depth = target->depth;
		item->search = binding;

		return bind ( ctx, scope, item->right, mode, aliases );

	}

	static bool bind ( run_context& ctx, prepared_select* scope, sql::expression* item, bind_mode mode, bool aliases ) {

		if ( !item )
			return true;

		bool fine = true;

		switch ( item->kind ) {

			case sql::EXPRESSION_MATCH:
				fine = bind_match ( ctx, scope, item, mode, aliases );
				break;

			case sql::EXPRESSION_LITERAL:
			case sql::EXPRESSION_PARAMETER:
				break;

			case sql::EXPRESSION_COLUMN: {

				if ( item->source >= 0 )
					break;

				if ( mode == BIND_CONSTANT ) {

					fine = fail_named ( ctx, "the column ", item->name, " cannot be used here, only constant values can" );

					break;

				}

				bool found = false;

				fine = resolve_column ( ctx, scope, item, found );

				if ( fine && !found && aliases && !item->table && scope && scope->syntax ) {

					for ( uint32_t i = 0; i < scope->syntax->column_count && !found; i++ ) {

						const sql::result_column& column = scope->syntax->columns [ i ];

						if ( column.alias && column.value && column.value != item && base::same_folded ( column.alias, item->name ) ) {

							*item = *column.value;
							found = true;

						}

					}

				}

				if ( fine && !found ) {

					base::text message;

					message.add ( "no such column: " );

					if ( item->table )
						message.add ( item->table ).add ( '.' );

					message.add ( item->name );
					fine = fail_run ( ctx, message.value ( ) );

				}

				break;

			}

			case sql::EXPRESSION_FUNCTION: {

				bool aggregate = false;
				int32_t id = function_id ( item->name, item->count, item->star, aggregate );

				if ( id < 0 ) {

					fine = function_named ( item->name ) ? fail_named ( ctx, "the function ", item->name, "() was given the wrong number of arguments" ) : fail_named ( ctx, "no such function: ", item->name, "" );

					break;

				}

				item->column = id;

				if ( search_function ( id ) ) {

					sql::expression* target = item->list [ 0 ];

					if ( target->source < 0 && !resolve_row ( scope, target ) ) {

						fine = fail_named ( ctx, "the function ", item->name, "() needs a table name as its first argument" );

						break;

					}

					for ( uint32_t i = 1; i < item->count && fine; i++ )
						fine = bind ( ctx, scope, item->list [ i ], mode, aliases );

					break;

				}

				if ( item->window ) {

					if ( mode != BIND_AGGREGATES ) {

						fine = fail_named ( ctx, "the window function ", item->name, "() can only be used in the result columns and ORDER BY" );

						break;

					}

					if ( !aggregate && !window_function ( id ) ) {

						fine = fail_named ( ctx, "the function ", item->name, "() cannot be used with OVER" );

						break;

					}

					if ( item->distinct ) {

						fine = fail_run ( ctx, "DISTINCT cannot be used inside a window function" );

						break;

					}

					if ( item->window_index < 0 ) {

						if ( !base::push ( *ctx.lasting, scope->windows, scope->window_count, scope->window_room, item ) ) {

							fine = fail_run ( ctx, "out of memory" );

							break;

						}

						item->window_index = static_cast < int32_t > ( scope->window_count - 1 );

					}

					sql::window_spec& spec = *item->window;

					fine = bind_children ( ctx, scope, item, BIND_AGGREGATES, false );

					for ( uint32_t i = 0; fine && i < spec.partition_count; i++ )
						fine = bind ( ctx, scope, spec.partition [ i ], BIND_AGGREGATES, false );

					for ( uint32_t i = 0; fine && i < spec.order_count; i++ )
						fine = bind ( ctx, scope, spec.order [ i ].value, BIND_AGGREGATES, false );

					fine = fine && bind ( ctx, scope, spec.start_offset, BIND_CONSTANT, false ) && bind ( ctx, scope, spec.end_offset, BIND_CONSTANT, false );

					break;

				}

				if ( window_function ( id ) ) {

					fine = fail_named ( ctx, "the window function ", item->name, "() needs an OVER clause after it" );

					break;

				}

				if ( item->distinct && !aggregate ) {

					fine = fail_named ( ctx, "DISTINCT only works inside an aggregate, not in ", item->name, "()" );

					break;

				}

				if ( aggregate ) {

					if ( mode != BIND_AGGREGATES ) {

						fine = fail_named ( ctx, "the aggregate ", item->name, mode == BIND_INSIDE_AGGREGATE ? "() cannot be used inside another aggregate" : "() cannot be used here" );

						break;

					}

					if ( item->aggregate < 0 ) {

						if ( !base::push ( *ctx.lasting, scope->aggregates, scope->aggregate_count, scope->aggregate_room, item ) ) {

							fine = fail_run ( ctx, "out of memory" );

							break;

						}

						item->aggregate = static_cast < int32_t > ( scope->aggregate_count - 1 );

					}

					fine = bind_children ( ctx, scope, item, BIND_INSIDE_AGGREGATE, false );

				} else
					fine = bind_children ( ctx, scope, item, mode, aliases );

				break;

			}

			case sql::EXPRESSION_SUBQUERY:
			case sql::EXPRESSION_EXISTS:
			case sql::EXPRESSION_IN: {

				if ( item->query ) {

					if ( mode == BIND_CONSTANT ) {

						fine = fail_run ( ctx, "a subquery cannot be used here" );

						break;

					}

					prepared_select* child = prepare_select ( ctx, item->query, scope );

					if ( !child ) {

						fine = false;

						break;

					}

					if ( item->kind != sql::EXPRESSION_EXISTS && child->output_count != 1 ) {

						base::text message;

						message.add ( "the subquery returns " ).number ( child->output_count ).add ( " columns where one is expected" );
						fine = fail_run ( ctx, message.value ( ) );

						break;

					}

					fine = add_child ( ctx, scope, child, item->kind == sql::EXPRESSION_SUBQUERY ? CHILD_SCALAR : item->kind == sql::EXPRESSION_IN ? CHILD_LIST : CHILD_EXISTS );

				}

				fine = fine && bind_children ( ctx, scope, item, mode, aliases );

				break;

			}

			default:
				fine = bind_children ( ctx, scope, item, mode, aliases );
				break;

		}

		return fine;

	}

	bool bind_expression ( run_context& ctx, prepared_select* scope, sql::expression* item, bind_mode mode ) {

		return bind ( ctx, scope, item, mode, false );

	}

	static const char* postgres_name ( run_context& ctx, const sql::expression* item ) {

		if ( !item )
			return nullptr;

		switch ( item->kind ) {

			case sql::EXPRESSION_COLUMN:
				return item->name;

			case sql::EXPRESSION_CAST: {

				const char* inner = postgres_name ( ctx, item->left );

				if ( inner )
					return inner;

				base::text type;

				for ( const char* at = item->name; at && *at && *at != '(' && *at != ' '; at++ )
					type.add ( base::lower ( *at ) );

				return ctx.lasting->keep ( type.value ( ), type.size );

			}

			case sql::EXPRESSION_FUNCTION: {

				base::text name;

				for ( const char* at = item->name; at && *at; at++ )
					name.add ( base::lower ( *at ) );

				return ctx.lasting->keep ( name.value ( ), name.size );

			}

			case sql::EXPRESSION_CASE:
				return "case";

			case sql::EXPRESSION_EXISTS:
				return "exists";

			default:
				return nullptr;

		}

	}

	static const char* output_name ( run_context& ctx, const sql::result_column& column ) {

		if ( column.alias )
			return column.alias;

		if ( ctx.schema->postgres ) {

			const char* name = postgres_name ( ctx, column.value );

			return name ? name : "?column?";

		}

		if ( column.value->kind == sql::EXPRESSION_COLUMN && column.value->name )
			return column.value->name;

		if ( column.written && *column.written )
			return column.written;

		base::text printed;

		sql::print_expression ( printed, column.value );

		return ctx.lasting->keep ( printed.value ( ), printed.size );

	}

	static bool add_output ( run_context& ctx, prepared_select* plan, sql::expression* value, const char* name, bool aliased, uint32_t& room ) {

		uint32_t names_room = room;
		uint32_t aliased_room = room;
		uint32_t count = plan->output_count;
		uint32_t aliased_count = plan->output_count;

		if ( !base::push ( *ctx.lasting, plan->outputs, plan->output_count, room, value ) || !base::push ( *ctx.lasting, plan->names, count, names_room, name ) || !base::push ( *ctx.lasting, plan->aliased, aliased_count, aliased_room, aliased ) )
			return fail_run ( ctx, "out of memory" );

		return true;

	}

	static bool push_filter ( run_context& ctx, prepared_select* plan, sql::expression* term, int32_t level ) {

		if ( level < 0 ) {

			if ( !base::push ( *ctx.lasting, plan->constant_filters, plan->constant_count, plan->constant_room, term ) )
				return fail_run ( ctx, "out of memory" );

			return true;

		}

		if ( level >= static_cast < int32_t > ( plan->source_count ) )
			level = static_cast < int32_t > ( plan->source_count ) - 1;

		prepared_source& source = plan->sources [ level ];

		if ( !base::push ( *ctx.lasting, source.filters, source.filter_count, source.filter_room, term ) )
			return fail_run ( ctx, "out of memory" );

		return true;

	}

	static bool fill_table_source ( run_context& ctx, prepared_source& out, table_info* table, const char* alias ) {

		out.table = table;
		out.alias = alias ? alias : table->name;
		out.width = table->column_count;
		out.names = ctx.lasting->make < const char* > ( out.width + 1 );
		out.affinities = ctx.lasting->make < sql::affinity_kind > ( out.width + 1 );
		out.hidden = ctx.lasting->make < bool > ( out.width + 1 );
		out.nocase = ctx.lasting->make < bool > ( out.width + 1 );
		out.used = ctx.lasting->make < bool > ( out.width + 1 );

		if ( !out.names || !out.affinities || !out.hidden || !out.nocase || !out.used )
			return fail_run ( ctx, "out of memory" );

		for ( uint32_t c = 0; c < out.width; c++ ) {

			out.names [ c ] = table->columns [ c ].name;
			out.affinities [ c ] = table->columns [ c ].affinity;
			out.nocase [ c ] = table->columns [ c ].nocase;

		}

		return true;

	}

	prepared_select* prepare_table_scope ( run_context& ctx, table_info* table, const char* alias, table_info* second, const char* second_alias ) {

		prepared_select* plan = ctx.lasting->make < prepared_select > ( 1 );

		if ( !plan ) {

			fail_run ( ctx, "out of memory" );

			return nullptr;

		}

		plan->id = ++ctx.next_id;
		plan->snapshot_aggregate = -1;
		plan->source_count = second ? 2 : 1;
		plan->sources = ctx.lasting->make < prepared_source > ( plan->source_count );

		if ( !plan->sources || !fill_table_source ( ctx, plan->sources [ 0 ], table, alias ) || ( second && !fill_table_source ( ctx, plan->sources [ 1 ], second, second_alias ) ) ) {

			fail_run ( ctx, "out of memory" );

			return nullptr;

		}

		return plan;

	}

	static bool expand_star ( run_context& ctx, prepared_select* plan, const sql::result_column& column, uint32_t& room ) {

		if ( !plan->source_count )
			return fail_run ( ctx, "* needs a table to take the columns from, add FROM and a table" );

		bool any = false;

		for ( uint32_t p = 0; p < plan->source_count; p++ ) {

			uint32_t s = 0;

			while ( s + 1 < plan->source_count && plan->sources [ s ].position != p )
				s++;

			prepared_source& source = plan->sources [ s ];

			if ( column.star_table && !base::same_folded ( column.star_table, source.alias ) )
				continue;

			any = true;

			for ( uint32_t c = 0; c < source.width; c++ ) {

				if ( !column.star_table && source.hidden [ c ] )
					continue;

				sql::expression* value = bound_column ( *ctx.lasting, s, static_cast < int32_t > ( c ), source.affinities [ c ], source.names [ c ], source.alias );

				if ( !value || !add_output ( ctx, plan, value, source.names [ c ], false, room ) )
					return fail_run ( ctx, "out of memory" );

				value->nocase = source.nocase && source.nocase [ c ];
				note_use ( source, static_cast < int32_t > ( c ) );

			}

		}

		if ( !any )
			return fail_named ( ctx, "no such table: ", column.star_table, "" );

		return true;

	}

	static bool pair_columns ( run_context& ctx, prepared_source& left, uint32_t left_column, uint32_t right, uint32_t right_column ) {

		if ( !left.paired_source ) {

			left.paired_source = ctx.lasting->make < int32_t > ( left.width + 1 );
			left.paired_column = ctx.lasting->make < int32_t > ( left.width + 1 );

			if ( !left.paired_source || !left.paired_column )
				return fail_run ( ctx, "out of memory" );

			for ( uint32_t c = 0; c < left.width; c++ )
				left.paired_source [ c ] = left.paired_column [ c ] = -1;

		}

		left.paired_source [ left_column ] = static_cast < int32_t > ( right );
		left.paired_column [ left_column ] = static_cast < int32_t > ( right_column );

		return true;

	}

	static bool natural_names ( run_context& ctx, prepared_select* plan, uint32_t right, const char**& names, uint32_t& count ) {

		prepared_source& source = plan->sources [ right ];
		uint32_t room = 0;

		names = nullptr;
		count = 0;

		for ( uint32_t c = 0; c < source.width; c++ ) {

			bool shared = false;

			for ( uint32_t s = 0; s < right && !shared; s++ )
				for ( uint32_t o = 0; o < plan->sources [ s ].width && !shared; o++ )
					shared = !plan->sources [ s ].hidden [ o ] && base::same_folded ( plan->sources [ s ].names [ o ], source.names [ c ] );

			if ( shared && !base::push ( *ctx.lasting, names, count, room, source.names [ c ] ) )
				return fail_run ( ctx, "out of memory" );

		}

		return true;

	}

	static bool join_using ( run_context& ctx, prepared_select* plan, uint32_t right, const char* const* names, uint32_t count, uint32_t& on_room ) {

		prepared_source& source = plan->sources [ right ];

		for ( uint32_t u = 0; u < count; u++ ) {

			const char* name = names [ u ];
			int32_t right_column = -1;
			int32_t left_source = -1;
			int32_t left_column = -1;

			for ( uint32_t c = 0; c < source.width && right_column < 0; c++ )
				if ( base::same_folded ( source.names [ c ], name ) )
					right_column = static_cast < int32_t > ( c );

			for ( uint32_t s = 0; s < right && left_source < 0; s++ ) {

				for ( uint32_t c = 0; c < plan->sources [ s ].width; c++ ) {

					if ( !plan->sources [ s ].hidden [ c ] && base::same_folded ( plan->sources [ s ].names [ c ], name ) ) {

						left_source = static_cast < int32_t > ( s );
						left_column = static_cast < int32_t > ( c );

						break;

					}

				}

			}

			if ( right_column < 0 || left_source < 0 )
				return fail_named ( ctx, "USING needs the column ", name, " on both sides of the join" );

			prepared_source& left = plan->sources [ left_source ];
			sql::expression* equal = ctx.lasting->make < sql::expression > ( 1 );

			if ( !equal )
				return fail_run ( ctx, "out of memory" );

			equal->kind = sql::EXPRESSION_BINARY;
			equal->op = sql::OPERATOR_EQUAL;
			equal->source = -1;
			equal->column = -1;
			equal->aggregate = -1;
			equal->left = bound_column ( *ctx.lasting, static_cast < uint32_t > ( left_source ), left_column, left.affinities [ left_column ], left.names [ left_column ], left.alias );
			equal->right = bound_column ( *ctx.lasting, right, right_column, source.affinities [ right_column ], source.names [ right_column ], source.alias );
			source.hidden [ right_column ] = true;
			note_use ( left, left_column );
			note_use ( source, right_column );

			if ( !equal->left || !equal->right || !base::push ( *ctx.lasting, source.on_terms, source.on_count, on_room, equal ) )
				return fail_run ( ctx, "out of memory" );

			if ( source.right && !pair_columns ( ctx, left, static_cast < uint32_t > ( left_column ), right, static_cast < uint32_t > ( right_column ) ) )
				return false;

		}

		return true;

	}

	static cte_entry* find_cte ( run_context& ctx, const char* name ) {

		for ( cte_scope* scope = ctx.ctes; scope; scope = scope->next )
			for ( uint32_t i = scope->count; i-- > 0; )
				if ( base::same_folded ( scope->entries [ i ].syntax->name, name ) )
					return &scope->entries [ i ];

		return nullptr;

	}

	static bool fill_cte_source ( run_context& ctx, prepared_select* plan, prepared_source& source, cte_entry* cte, const char* alias ) {

		source.cte = cte;
		source.alias = alias ? alias : cte->syntax->name;

		if ( cte->preparing ) {

			if ( !cte->width )
				return fail_named ( ctx, "the recursive WITH query ", cte->syntax->name, " must start with a part that does not refer to itself" );

			source.working = true;
			plan->uses_working = true;

		} else {

			source.query = cte->plan;

			if ( !add_child ( ctx, plan, cte->plan, CHILD_FROM ) )
				return false;

			if ( cte->plan->correlated )
				plan->correlated = true;

		}

		source.width = cte->width;
		source.names = cte->names;
		source.affinities = ctx.lasting->make < sql::affinity_kind > ( source.width + 1 );
		source.hidden = ctx.lasting->make < bool > ( source.width + 1 );

		if ( !source.affinities || !source.hidden )
			return fail_run ( ctx, "out of memory" );

		return true;

	}

	static prepared_select* prepare_core ( run_context& ctx, sql::select_statement* item, prepared_select* parent ) {

		prepared_select* plan = ctx.lasting->make < prepared_select > ( 1 );

		if ( !plan ) {

			fail_run ( ctx, "out of memory" );

			return nullptr;

		}

		plan->syntax = item;
		plan->parent = parent;
		plan->id = ++ctx.next_id;
		plan->snapshot_aggregate = -1;
		plan->distinct = item->distinct;

		uint32_t output_room = 0;

		if ( item->values ) {

			plan->values_only = true;

			for ( uint32_t c = 0; c < item->value_columns; c++ ) {

				base::text name;

				name.add ( "column" ).number ( c + 1 );

				const char* kept = ctx.lasting->keep ( name.value ( ), name.size );

				if ( !kept || !add_output ( ctx, plan, nullptr, kept, true, output_room ) )
					return nullptr;

			}

			for ( uint32_t i = 0; i < item->value_rows * item->value_columns; i++ )
				if ( !bind_expression ( ctx, plan, item->values [ i ], BIND_PLAIN ) )
					return nullptr;

			return plan;

		}

		plan->source_count = item->source_count;

		if ( item->source_count ) {

			plan->sources = ctx.lasting->make < prepared_source > ( item->source_count );

			if ( !plan->sources ) {

				fail_run ( ctx, "out of memory" );

				return nullptr;

			}

		}

		for ( uint32_t i = 0; i < item->source_count; i++ ) {

			const sql::table_source& syntax = item->sources [ i ];
			prepared_source& source = plan->sources [ i ];

			source.syntax = &syntax;
			source.position = i;
			source.left = syntax.join == sql::JOIN_LEFT || syntax.join == sql::JOIN_FULL;
			source.right = syntax.join == sql::JOIN_RIGHT || syntax.join == sql::JOIN_FULL;

			sql::select_statement* view = nullptr;

			if ( !syntax.query && syntax.name && !find_cte ( ctx, syntax.name ) && !find_table ( *ctx.schema, syntax.name ) ) {

				view = catalog_view ( ctx, syntax.name );

				if ( !view && ctx.problem->size )
					return nullptr;

			}

			if ( syntax.query || view ) {

				source.query = prepare_select ( ctx, view ? view : syntax.query, parent );

				if ( !source.query || !add_child ( ctx, plan, source.query, CHILD_FROM ) )
					return nullptr;

				if ( source.query->correlated )
					plan->correlated = true;

				source.alias = syntax.alias || !view ? syntax.alias : catalog_view_alias ( syntax.name );
				source.width = source.query->output_count;
				source.names = source.query->names;
				source.affinities = ctx.lasting->make < sql::affinity_kind > ( source.width + 1 );
				source.hidden = ctx.lasting->make < bool > ( source.width + 1 );

				if ( !source.affinities || !source.hidden ) {

					fail_run ( ctx, "out of memory" );

					return nullptr;

				}

				for ( uint32_t c = 0; c < source.width; c++ )
					source.affinities [ c ] = source.query->values_only ? sql::AFFINITY_ANY : expression_affinity ( source.query->outputs [ c ] );

			} else if ( cte_entry* cte = find_cte ( ctx, syntax.name ) ) {

				if ( !fill_cte_source ( ctx, plan, source, cte, syntax.alias ) )
					return nullptr;

			} else {

				table_info* table = find_table ( *ctx.schema, syntax.name );

				if ( !table ) {

					fail_named ( ctx, "no such table: ", syntax.name, "" );

					return nullptr;

				}

				if ( !fill_table_source ( ctx, source, table, syntax.alias ) )
					return nullptr;

			}

		}

		order_joins ( ctx, plan );

		for ( uint32_t i = 0; i < item->source_count; i++ ) {

			prepared_source& source = plan->sources [ i ];
			uint32_t on_room = 0;

			if ( source.syntax->natural ) {

				const char** names = nullptr;
				uint32_t count = 0;

				if ( source.syntax->using_count || source.syntax->on ) {

					fail_run ( ctx, "a NATURAL join cannot also have ON or USING" );

					return nullptr;

				}

				if ( !natural_names ( ctx, plan, i, names, count ) || !join_using ( ctx, plan, i, names, count, on_room ) )
					return nullptr;

			}

			if ( source.syntax->using_count && !join_using ( ctx, plan, i, source.syntax->using_names, source.syntax->using_count, on_room ) )
				return nullptr;

			if ( source.syntax->on ) {

				sql::expression* on = source.syntax->on;

				if ( !bind_expression ( ctx, plan, on, BIND_PLAIN ) || !split_terms ( ctx, on, source.on_terms, source.on_count, on_room ) )
					return nullptr;

			}

			for ( uint32_t t = 0; t < source.on_count; t++ ) {

				if ( highest_position ( plan, source.on_terms [ t ] ) > static_cast < int32_t > ( source.position ) ) {

					fail_run ( ctx, "an ON clause can only use tables joined before it" );

					return nullptr;

				}

			}

		}

		for ( uint32_t i = 0; i < item->column_count; i++ ) {

			const sql::result_column& column = item->columns [ i ];

			if ( column.star ) {

				if ( !expand_star ( ctx, plan, column, output_room ) )
					return nullptr;

				continue;

			}

			if ( !bind ( ctx, plan, column.value, BIND_AGGREGATES, false ) || !add_output ( ctx, plan, column.value, output_name ( ctx, column ), column.alias != nullptr, output_room ) )
				return nullptr;

		}

		int32_t outer_floor = -1;

		for ( uint32_t i = 0; i < plan->source_count; i++ )
			if ( plan->sources [ i ].right )
				outer_floor = static_cast < int32_t > ( i );

		if ( item->where ) {

			sql::expression** terms = nullptr;
			uint32_t count = 0;
			uint32_t room = 0;

			if ( !bind_expression ( ctx, plan, item->where, BIND_PLAIN ) || !split_terms ( ctx, item->where, terms, count, room ) )
				return nullptr;

			for ( uint32_t t = 0; t < count; t++ ) {

				int32_t level = highest_source ( terms [ t ], 0 );

				if ( level >= 0 && level < outer_floor )
					level = outer_floor;

				if ( !push_filter ( ctx, plan, terms [ t ], level ) )
					return nullptr;

			}

		}

		for ( uint32_t i = 0; i < plan->source_count; i++ ) {

			prepared_source& source = plan->sources [ i ];

			if ( source.left || source.right )
				continue;

			for ( uint32_t t = 0; t < source.on_count; t++ )
				if ( !push_filter ( ctx, plan, source.on_terms [ t ], highest_source ( source.on_terms [ t ], 0 ) ) )
					return nullptr;

			source.on_count = 0;

		}

		if ( item->group_count ) {

			plan->group = ctx.lasting->make < sql::expression* > ( item->group_count );
			plan->group_count = item->group_count;

			if ( !plan->group ) {

				fail_run ( ctx, "out of memory" );

				return nullptr;

			}

			for ( uint32_t i = 0; i < item->group_count; i++ ) {

				sql::expression* term = item->group [ i ];

				if ( term->kind == sql::EXPRESSION_LITERAL && term->literal.kind == storage::VALUE_INTEGER ) {

					if ( term->literal.integer < 1 || term->literal.integer > plan->output_count || !plan->outputs [ term->literal.integer - 1 ] ) {

						fail_run ( ctx, "a GROUP BY number must point at a result column" );

						return nullptr;

					}

					term = plan->outputs [ term->literal.integer - 1 ];

				} else if ( !bind ( ctx, plan, term, BIND_PLAIN, true ) )
					return nullptr;

				if ( contains_aggregate ( term ) ) {

					fail_run ( ctx, "an aggregate cannot be used in GROUP BY" );

					return nullptr;

				}

				plan->group [ i ] = term;

			}

		}

		if ( item->having ) {

			plan->having = item->having;

			if ( !bind ( ctx, plan, plan->having, BIND_AGGREGATES, true ) )
				return nullptr;

		}

		for ( uint32_t i = 0; i < plan->source_count; i++ )
			choose_path ( ctx, plan, i );

		return plan;

	}

	static bool prepare_order ( run_context& ctx, prepared_select* plan, const sql::select_statement* item ) {

		plan->order_count = item->order_count;
		plan->order_values = ctx.lasting->make < sql::expression* > ( item->order_count );
		plan->order_output = ctx.lasting->make < int32_t > ( item->order_count );
		plan->order_desc = ctx.lasting->make < bool > ( item->order_count );
		plan->order_nocase = ctx.lasting->make < bool > ( item->order_count );

		if ( !plan->order_values || !plan->order_output || !plan->order_desc || !plan->order_nocase )
			return fail_run ( ctx, "out of memory" );

		for ( uint32_t i = 0; i < item->order_count; i++ ) {

			sql::expression* term = item->order [ i ].value;

			plan->order_desc [ i ] = item->order [ i ].descending;
			plan->order_output [ i ] = -1;

			if ( term->kind == sql::EXPRESSION_LITERAL && term->literal.kind == storage::VALUE_INTEGER ) {

				if ( term->literal.integer < 1 || term->literal.integer > plan->output_count ) {

					base::text message;

					message.add ( "ORDER BY " ).signed_number ( term->literal.integer ).add ( " is out of range, use 1 to " ).number ( plan->output_count );

					return fail_run ( ctx, message.value ( ) );

				}

				plan->order_output [ i ] = static_cast < int32_t > ( term->literal.integer - 1 );

				continue;

			}

			if ( term->kind == sql::EXPRESSION_COLUMN && !term->table && term->source < 0 ) {

				for ( uint32_t c = 0; c < plan->output_count; c++ ) {

					if ( ( plan->next || plan->aliased [ c ] ) && plan->names [ c ] && base::same_folded ( plan->names [ c ], term->name ) ) {

						plan->order_output [ i ] = static_cast < int32_t > ( c );

						break;

					}

				}

				for ( uint32_t c = 0; c < plan->output_count && plan->order_output [ i ] < 0 && !plan->next && !plan->values_only; c++ ) {

					const sql::expression* output = plan->outputs [ c ];

					if ( output && output->kind == sql::EXPRESSION_COLUMN && output->table && output->name && base::same_folded ( output->name, term->name ) )
						plan->order_output [ i ] = static_cast < int32_t > ( c );

				}

				if ( plan->order_output [ i ] >= 0 )
					continue;

			}

			if ( plan->next || plan->values_only ) {

				base::text printed;

				sql::print_expression ( printed, term );

				for ( uint32_t c = 0; c < plan->output_count && plan->order_output [ i ] < 0; c++ )
					if ( base::same_folded ( plan->names [ c ], printed.value ( ) ) )
						plan->order_output [ i ] = static_cast < int32_t > ( c );

				if ( plan->order_output [ i ] < 0 )
					return fail_run ( ctx, "an ORDER BY term of a compound query must name a result column" );

				continue;

			}

			if ( !bind ( ctx, plan, term, BIND_AGGREGATES, false ) )
				return false;

			plan->order_values [ i ] = term;

		}

		for ( uint32_t i = 0; i < item->order_count; i++ ) {

			const sql::expression* value = plan->order_values [ i ] ? plan->order_values [ i ] : plan->order_output [ i ] >= 0 ? plan->outputs [ plan->order_output [ i ] ] : nullptr;

			plan->order_nocase [ i ] = item->order [ i ].value->nocase || ( value && value->nocase );

		}

		return true;

	}

	static int32_t order_column ( const prepared_select* plan, uint32_t term ) {

		const sql::expression* value = plan->order_values [ term ];

		if ( !value && plan->order_output [ term ] >= 0 )
			value = plan->outputs [ plan->order_output [ term ] ];

		if ( !value || value->kind != sql::EXPRESSION_COLUMN || value->depth != 0 || value->source != 0 )
			return -3;

		const table_info& table = *plan->sources [ 0 ].table;

		return value->column < 0 || value->column == table.rowid_column ? -1 : value->column;

	}

	static bool index_matches_order ( const prepared_select* plan, const index_info& index, uint32_t equal_count ) {

		uint32_t position = equal_count;

		for ( uint32_t t = 0; t < plan->order_count; t++ ) {

			int32_t column = order_column ( plan, t );
			bool skipped = false;

			for ( uint32_t k = 0; k < equal_count && !skipped; k++ )
				skipped = static_cast < int32_t > ( index.columns [ k ] ) == column;

			if ( skipped )
				continue;

			if ( column == -1 )
				return position == index.column_count;

			if ( position >= index.column_count || static_cast < int32_t > ( index.columns [ position ] ) != column )
				return false;

			position++;

		}

		return true;

	}

	static void order_through_index ( run_context& ctx, prepared_select* plan ) {

		if ( plan->grouping || plan->distinct || plan->window_count || plan->next || plan->values_only || plan->source_count == 0 || !plan->order_count || !plan->sources [ 0 ].table || plan->sources [ 0 ].left )
			return;

		for ( uint32_t t = 0; t < plan->order_count; t++ )
			if ( plan->order_desc [ t ] || plan->order_nocase [ t ] || order_column ( plan, t ) == -3 )
				return;

		for ( uint32_t s = 0; s < plan->source_count; s++ )
			if ( plan->sources [ s ].right )
				return;

		prepared_source& source = plan->sources [ 0 ];
		access_path& path = source.path;

		if ( path.kind != PATH_INDEX ) {

			if ( order_column ( plan, 0 ) == -1 ) {

				plan->order_by_index = true;

				return;

			}

			if ( path.kind != PATH_SCAN || !plan->limit )
				return;

			for ( uint32_t i = 0; i < ctx.schema->index_count; i++ ) {

				index_info& index = ctx.schema->indexes [ i ];

				if ( index.fulltext || !base::same_folded ( index.table, source.table->name ) || !index_matches_order ( plan, index, 0 ) )
					continue;

				path = { };
				path.kind = PATH_INDEX;
				path.index = &index;
				plan->order_by_index = true;

				return;

			}

			return;

		}

		plan->order_by_index = index_matches_order ( plan, *path.index, path.equal_count );

	}

	static void mark_covering ( prepared_select* plan ) {

		for ( uint32_t s = 0; s < plan->source_count; s++ ) {

			prepared_source& source = plan->sources [ s ];
			access_path& path = source.path;

			if ( !source.table || !source.used || path.kind != PATH_INDEX )
				continue;

			bool covered = true;

			for ( uint32_t c = 0; c < source.width && covered; c++ ) {

				if ( !source.used [ c ] || static_cast < int32_t > ( c ) == source.table->rowid_column )
					continue;

				bool held = false;

				for ( uint32_t k = 0; k < path.index->column_count && !held; k++ )
					held = path.index->columns [ k ] == c;

				covered = held;

			}

			path.covering = covered;

		}

	}

	static void finish_flags ( run_context& ctx, prepared_select* plan ) {

		plan->grouping = plan->group_count || plan->aggregate_count;

		if ( plan->window_count ) {

			plan->window_offsets = ctx.lasting->make < uint32_t > ( plan->window_count );
			plan->window_width = 0;

			for ( uint32_t w = 0; plan->window_offsets && w < plan->window_count; w++ ) {

				const sql::expression* call = plan->windows [ w ];

				plan->window_offsets [ w ] = plan->window_width;
				plan->window_width += call->count + call->window->partition_count + call->window->order_count;

			}

		}
		plan->snapshot_aggregate = -1;

		if ( plan->aggregate_count == 1 && ( plan->aggregates [ 0 ]->column == FUNCTION_MIN_AGGREGATE || plan->aggregates [ 0 ]->column == FUNCTION_MAX_AGGREGATE ) )
			plan->snapshot_aggregate = 0;

	}

	prepared_select* prepare_select ( run_context& ctx, sql::select_statement* item, prepared_select* parent ) {

		if ( item->prepared )
			return static_cast < prepared_select* > ( item->prepared );

		if ( ++ctx.depth > RUN_DEPTH ) {

			ctx.depth--;
			fail_run ( ctx, "the query nests subqueries too deeply" );

			return nullptr;

		}

		cte_scope* scope = nullptr;

		if ( item->with_count ) {

			scope = ctx.lasting->make < cte_scope > ( 1 );

			if ( !scope || !( scope->entries = ctx.lasting->make < cte_entry > ( item->with_count ) ) ) {

				ctx.depth--;
				fail_run ( ctx, "out of memory" );

				return nullptr;

			}

			scope->next = ctx.ctes;
			ctx.ctes = scope;

			for ( uint32_t i = 0; i < item->with_count; i++ ) {

				cte_entry& entry = scope->entries [ i ];
				const sql::common_table& table = item->with [ i ];

				entry.syntax = &table;

				if ( item->recursive ) {

					entry.preparing = true;
					scope->count = i + 1;

				}

				cte_entry* saved = ctx.pending_cte;

				ctx.pending_cte = &entry;

				prepared_select* body = prepare_select ( ctx, table.query, parent );

				ctx.pending_cte = saved;
				entry.preparing = false;

				if ( body && !entry.width ) {

					entry.names = table.column_count ? table.columns : body->names;
					entry.width = table.column_count ? table.column_count : body->output_count;

				}

				if ( body && table.column_count && table.column_count != body->output_count ) {

					fail_named ( ctx, "the WITH query ", table.name, " names a different number of columns than it returns" );
					body = nullptr;

				}

				if ( body ) {

					entry.plan = body;

					for ( prepared_select* core = body; core; core = core->next ) {

						if ( !core->uses_working )
							continue;

						if ( !entry.recursive ) {

							sql::compound_kind joiner = sql::COMPOUND_NONE;

							for ( prepared_select* before = body; before && before != core; before = before->next )
								joiner = before->compound;

							if ( joiner != sql::COMPOUND_UNION && joiner != sql::COMPOUND_UNION_ALL ) {

								fail_named ( ctx, "the recursive WITH query ", table.name, " must join its parts with UNION or UNION ALL" );
								body = nullptr;

								break;

							}

							entry.distinct = joiner == sql::COMPOUND_UNION;

						}

						entry.recursive = true;

					}

				}

				if ( !body ) {

					ctx.ctes = scope->next;
					ctx.depth--;

					return nullptr;

				}

				scope->count = i + 1;

			}

		}

		prepared_select* first = prepare_core ( ctx, item, parent );
		prepared_select* last = first;

		if ( first && ctx.pending_cte && ctx.pending_cte->syntax->query == item && !ctx.pending_cte->width ) {

			const sql::common_table* table = ctx.pending_cte->syntax;

			ctx.pending_cte->names = table->column_count ? table->columns : first->names;
			ctx.pending_cte->width = table->column_count ? table->column_count : first->output_count;

		}

		for ( sql::select_statement* core = item; first && core->next; core = core->next ) {

			last->compound = core->compound;
			last->next = prepare_core ( ctx, core->next, parent );

			if ( !last->next ) {

				first = nullptr;

				break;

			}

			if ( last->next->output_count != first->output_count ) {

				fail_run ( ctx, "every SELECT in a UNION, INTERSECT or EXCEPT needs the same number of columns" );
				first = nullptr;

				break;

			}

			if ( last->next->correlated )
				first->correlated = true;

			last = last->next;

		}

		if ( first && item->order_count && !prepare_order ( ctx, first, item ) )
			first = nullptr;

		if ( first ) {

			first->limit = item->limit;
			first->offset = item->offset;

			if ( !bind_expression ( ctx, first, first->limit, BIND_CONSTANT ) || !bind_expression ( ctx, first, first->offset, BIND_CONSTANT ) )
				first = nullptr;

		}

		for ( prepared_select* core = first; core; core = core->next ) {

			finish_flags ( ctx, core );

			if ( core->syntax && core->syntax->having && !core->grouping ) {

				fail_run ( ctx, "HAVING needs GROUP BY or an aggregate" );
				first = nullptr;

				break;

			}

		}

		if ( first )
			order_through_index ( ctx, first );

		for ( prepared_select* core = first; core; core = core->next )
			mark_covering ( core );

		if ( scope )
			ctx.ctes = scope->next;

		ctx.depth--;
		item->prepared = first;

		return first;

	}

}
