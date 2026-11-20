#include "engine/search/search.hh"
#include "sql/print/print.hh"

namespace engine {

	static bool numeric_affinity ( sql::affinity_kind affinity ) {

		return affinity == sql::AFFINITY_INTEGER || affinity == sql::AFFINITY_REAL || affinity == sql::AFFINITY_NUMERIC;

	}

	static bool seek_allowed ( sql::affinity_kind column, sql::affinity_kind other ) {

		if ( numeric_affinity ( column ) )
			return true;

		if ( column == sql::AFFINITY_TEXT )
			return !numeric_affinity ( other );

		return !numeric_affinity ( other ) && other != sql::AFFINITY_TEXT;

	}

	static bool usable_other ( const sql::expression* other, uint32_t level ) {

		int32_t highest = highest_source ( other, 0 );

		return highest < static_cast < int32_t > ( level );

	}

	static bool column_at ( const sql::expression* item, uint32_t level ) {

		return item && item->kind == sql::EXPRESSION_COLUMN && item->depth == 0 && item->source == static_cast < int32_t > ( level );

	}

	static sql::operator_kind flipped ( sql::operator_kind op ) {

		switch ( op ) {

			case sql::OPERATOR_LESS: return sql::OPERATOR_GREATER;
			case sql::OPERATOR_LESS_EQUAL: return sql::OPERATOR_GREATER_EQUAL;
			case sql::OPERATOR_GREATER: return sql::OPERATOR_LESS;
			case sql::OPERATOR_GREATER_EQUAL: return sql::OPERATOR_LESS_EQUAL;
			default: return op;

		}

	}

	static void collect_bounds ( sql::expression* const* terms, uint32_t count, uint32_t level, int32_t column, int32_t rowid_column, sql::affinity_kind affinity, sql::expression*& equal, sql::expression*& low, bool& low_inclusive, sql::expression*& high, bool& high_inclusive, sql::expression*& list ) {

		auto same_column = [ & ] ( const sql::expression* item ) {

			if ( !column_at ( item, level ) )
				return false;

			if ( column < 0 )
				return item->column < 0 || item->column == rowid_column;

			return item->column == column;

		};

		for ( uint32_t t = 0; t < count; t++ ) {

			sql::expression* term = terms [ t ];

			if ( ( term->left && term->left->nocase ) || ( term->right && term->right->nocase ) || ( term->third && term->third->nocase ) )
				continue;

			if ( term->kind == sql::EXPRESSION_BINARY ) {

				sql::operator_kind op = term->op;
				sql::expression* other = nullptr;

				if ( op != sql::OPERATOR_EQUAL && op != sql::OPERATOR_LESS && op != sql::OPERATOR_LESS_EQUAL && op != sql::OPERATOR_GREATER && op != sql::OPERATOR_GREATER_EQUAL )
					continue;

				if ( same_column ( term->left ) && usable_other ( term->right, level ) )
					other = term->right;
				else if ( same_column ( term->right ) && usable_other ( term->left, level ) ) {

					other = term->left;
					op = flipped ( op );

				}

				if ( !other || !seek_allowed ( affinity, expression_affinity ( other ) ) )
					continue;

				if ( op == sql::OPERATOR_EQUAL ) {

					if ( !equal )
						equal = other;

				} else if ( op == sql::OPERATOR_GREATER || op == sql::OPERATOR_GREATER_EQUAL ) {

					if ( !low ) {

						low = other;
						low_inclusive = op == sql::OPERATOR_GREATER_EQUAL;

					}

				} else if ( !high ) {

					high = other;
					high_inclusive = op == sql::OPERATOR_LESS_EQUAL;

				}

			} else if ( term->kind == sql::EXPRESSION_BETWEEN && !term->negated && same_column ( term->left ) && usable_other ( term->right, level ) && usable_other ( term->third, level ) && seek_allowed ( affinity, expression_affinity ( term->right ) ) && seek_allowed ( affinity, expression_affinity ( term->third ) ) ) {

				if ( !low && !high ) {

					low = term->right;
					low_inclusive = true;
					high = term->third;
					high_inclusive = true;

				}

			} else if ( term->kind == sql::EXPRESSION_IN && !term->negated && term->query && same_column ( term->left ) && !list ) {

				const prepared_select* child = static_cast < const prepared_select* > ( term->query->prepared );

				if ( child && !child->correlated && child->output_count == 1 )
					list = term;

			} else if ( term->kind == sql::EXPRESSION_IN && !term->negated && !term->query && term->count && same_column ( term->left ) && !list ) {

				bool fine = true;

				for ( uint32_t i = 0; i < term->count && fine; i++ )
					fine = usable_other ( term->list [ i ], level ) && seek_allowed ( affinity, expression_affinity ( term->list [ i ] ) );

				if ( fine )
					list = term;

			}

		}

	}

	static bool bound_term ( const sql::expression* term, const access_path& path ) {

		if ( term->kind == sql::EXPRESSION_BETWEEN )
			return !term->negated && path.low && path.high && term->right == path.low && term->third == path.high;

		if ( term->kind != sql::EXPRESSION_BINARY )
			return false;

		for ( uint32_t k = 0; k < path.equal_count; k++ )
			if ( term->left == path.equal [ k ] || term->right == path.equal [ k ] )
				return term->op == sql::OPERATOR_EQUAL;

		if ( path.low && ( term->left == path.low || term->right == path.low ) )
			return true;

		return path.low && path.high && ( term->left == path.high || term->right == path.high );

	}

	static void consume_bounds ( prepared_source& source ) {

		uint32_t end = source.filter_count;

		for ( uint32_t t = 0; t < end; ) {

			sql::expression* term = source.filters [ t ];

			if ( !bound_term ( term, source.path ) ) {

				t++;

				continue;

			}

			end--;
			source.filters [ t ] = source.filters [ end ];
			source.filters [ end ] = term;

		}

		source.path.consumed = source.filter_count - end;

	}

	void choose_path ( run_context& ctx, prepared_select* plan, uint32_t level ) {

		prepared_source& source = plan->sources [ level ];

		source.path = { };
		source.path.kind = PATH_SCAN;

		if ( !source.table )
			return;

		bool outer = source.left || source.right;
		sql::expression* const* terms = outer ? source.on_terms : source.filters;
		uint32_t count = outer ? source.on_count : source.filter_count;

		if ( !count )
			return;

		table_info& table = *source.table;
		sql::expression* equal = nullptr;
		sql::expression* low = nullptr;
		sql::expression* high = nullptr;
		sql::expression* list = nullptr;
		bool low_inclusive = false;
		bool high_inclusive = false;

		collect_bounds ( terms, count, level, -1, table.rowid_column, sql::AFFINITY_INTEGER, equal, low, low_inclusive, high, high_inclusive, list );

		if ( equal ) {

			source.path.kind = PATH_ROWID_EQUAL;
			source.path.equal [ 0 ] = equal;
			source.path.equal_count = 1;

			if ( !outer )
				consume_bounds ( source );

			return;

		}

		for ( uint32_t t = 0; t < count; t++ ) {

			sql::expression* term = terms [ t ];
			const search_binding* binding = static_cast < const search_binding* > ( term->search );

			if ( term->kind != sql::EXPRESSION_MATCH || term->negated || !binding || binding->depth || binding->source != static_cast < int32_t > ( level ) || !usable_other ( term->right, level ) )
				continue;

			source.path.kind = PATH_FULLTEXT;
			source.path.list = term;
			source.path.index = binding->index;

			return;

		}

		access_path best_index = { };
		int32_t best_score = 0;

		for ( uint32_t i = 0; i < ctx.schema->index_count; i++ ) {

			index_info& index = ctx.schema->indexes [ i ];

			if ( index.fulltext || !owned_by ( index, table ) || !index.column_count )
				continue;

			access_path path = { };

			path.kind = PATH_INDEX;
			path.index = &index;

			for ( uint32_t k = 0; k < index.column_count; k++ ) {

				sql::expression* column_equal = nullptr;
				sql::expression* column_low = nullptr;
				sql::expression* column_high = nullptr;
				sql::expression* column_list = nullptr;
				bool column_low_inclusive = false;
				bool column_high_inclusive = false;
				uint32_t column = index.columns [ k ];

				collect_bounds ( terms, count, level, static_cast < int32_t > ( column ), -2, table.columns [ column ].affinity, column_equal, column_low, column_low_inclusive, column_high, column_high_inclusive, column_list );

				if ( column_equal ) {

					path.equal [ path.equal_count++ ] = column_equal;

					continue;

				}

				path.low = column_low;
				path.low_inclusive = column_low_inclusive;
				path.high = column_high;
				path.high_inclusive = column_high_inclusive;

				break;

			}

			int32_t score = static_cast < int32_t > ( path.equal_count ) * 8 + ( path.low ? 2 : 0 ) + ( path.high ? 2 : 0 );

			if ( index.unique && path.equal_count == index.column_count )
				score += 1000;

			if ( score > best_score ) {

				best_score = score;
				best_index = path;

			}

		}

		if ( best_score >= 8 ) {

			source.path = best_index;

			if ( !outer )
				consume_bounds ( source );

			return;

		}

		if ( list ) {

			source.path.kind = PATH_ROWID_LIST;
			source.path.list = list;

			for ( uint32_t t = 0; !outer && t < source.filter_count; t++ ) {

				if ( source.filters [ t ] != list )
					continue;

				source.filters [ t ] = source.filters [ source.filter_count - 1 ];
				source.filters [ source.filter_count - 1 ] = list;
				source.path.consumed = 1;

				break;

			}

			return;

		}

		if ( low || high ) {

			source.path.kind = PATH_ROWID_RANGE;
			source.path.low = low;
			source.path.low_inclusive = low_inclusive;
			source.path.high = high;
			source.path.high_inclusive = high_inclusive;

			return;

		}

		if ( best_score > 0 ) {

			source.path = best_index;

			if ( !outer )
				consume_bounds ( source );

		}

	}

	void describe_path ( const prepared_source& source, base::text& out ) {

		if ( source.working ) {

			out.add ( "SCAN " ).add ( source.alias ).add ( " (rows from the last recursive step)" );

			return;

		}

		if ( source.query ) {

			out.add ( "SCAN " ).add ( source.alias ).add ( source.cte ? " (WITH query " : " (subquery " ).number ( source.query->id ).add ( ')' );

			return;

		}

		const access_path& path = source.path;

		out.add ( path.kind == PATH_SCAN ? "SCAN " : "SEARCH " ).add ( source.table->name );

		if ( !base::same_folded ( source.alias, source.table->name ) )
			out.add ( " AS " ).add ( source.alias );

		switch ( path.kind ) {

			case PATH_SCAN:
				break;

			case PATH_ROWID_EQUAL:
				out.add ( " USING ROWID (rowid=?)" );
				break;

			case PATH_ROWID_LIST:
				out.add ( path.list->query ? " USING ROWID (rowid IN subquery)" : " USING ROWID (rowid IN list)" );
				break;

			case PATH_FULLTEXT:
				out.add ( " USING FULLTEXT INDEX " ).add ( path.index->name ).add ( " (MATCH)" );
				break;

			case PATH_ROWID_RANGE:
				out.add ( " USING ROWID (" );

				if ( path.low )
					out.add ( path.low_inclusive ? "rowid>=?" : "rowid>?" );

				if ( path.low && path.high )
					out.add ( " AND " );

				if ( path.high )
					out.add ( path.high_inclusive ? "rowid<=?" : "rowid<?" );

				out.add ( ')' );
				break;

			case PATH_INDEX: {

				const index_info& index = *path.index;

				if ( path.covering )
					out.add ( " USING COVERING INDEX " ).add ( index.name );
				else
					out.add ( index.unique && path.equal_count == index.column_count ? " USING UNIQUE INDEX " : " USING INDEX " ).add ( index.name );

				if ( !path.equal_count && !path.low && !path.high )
					break;

				out.add ( " (" );

				for ( uint32_t k = 0; k < path.equal_count; k++ ) {

					if ( k )
						out.add ( " AND " );

					out.add ( source.table->columns [ index.columns [ k ] ].name ).add ( "=?" );

				}

				if ( path.low || path.high ) {

					const char* name = source.table->columns [ index.columns [ path.equal_count ] ].name;

					if ( path.equal_count )
						out.add ( " AND " );

					if ( path.low )
						out.add ( name ).add ( path.low_inclusive ? ">=?" : ">?" );

					if ( path.low && path.high )
						out.add ( " AND " );

					if ( path.high )
						out.add ( name ).add ( path.high_inclusive ? "<=?" : "<?" );

				}

				out.add ( ')' );
				break;

			}

		}

	}

	bool fallback_value ( run_context& ctx, const column_info& column, storage::value& out ) {

		if ( !column.fallback ) {

			out = storage::null_value ( );

			return true;

		}

		if ( !bind_expression ( ctx, nullptr, column.fallback, BIND_CONSTANT ) || !evaluate ( ctx, nullptr, column.fallback, out ) )
			return false;

		out = apply_affinity ( *ctx.scratch, out, column.affinity );

		return true;

	}

	bool decode_row ( run_context& ctx, const table_info& table, const uint8_t* payload, size_t size, int64_t rowid, storage::value* values ) {

		uint32_t count = 0;

		if ( !storage::decode_record ( payload, size, values, table.column_count, count ) )
			return fail_named ( ctx, "a row of table ", table.name, " is damaged" );

		for ( uint32_t i = count; i < table.column_count; i++ )
			if ( !fallback_value ( ctx, table.columns [ i ], values [ i ] ) )
				return false;

		if ( table.rowid_column >= 0 )
			values [ table.rowid_column ] = storage::integer_value ( rowid );

		if ( count < table.column_count && !compute_generated ( ctx, table, values, rowid, count ) )
			return false;

		return true;

	}

	static bool decode_bytes ( run_context& ctx, const table_info& table, int64_t rowid, const uint8_t* data, size_t size, storage::value*& values ) {

		values = static_cast < storage::value* > ( ctx.scratch->take_raw ( sizeof ( storage::value ) * ( table.column_count + 1 ) ) );

		if ( !values )
			return fail_run ( ctx, "out of memory" );

		values [ table.column_count ] = storage::null_value ( );

		return decode_row ( ctx, table, data, size, rowid, values );

	}

	static bool has_fulltext ( run_context& ctx, const table_info& table ) {

		for ( uint32_t i = 0; i < ctx.schema->index_count; i++ )
			if ( ctx.schema->indexes [ i ].fulltext && owned_by ( ctx.schema->indexes [ i ], table ) )
				return true;

		return false;

	}

	static uint32_t decode_limit ( run_context& ctx, source_state& source ) {

		if ( source.decode_ready )
			return source.decode_limit;

		if ( source.whole ) {

			source.decode_limit = RECORD_COLUMNS + 1;
			source.decode_ready = true;

			return source.decode_limit;

		}

		const prepared_source& plan = *source.plan;
		const table_info& table = *plan.table;
		uint32_t limit = table.column_count;

		bool partial = false;

		if ( plan.used && !table.generated_count && plan.width == table.column_count && !has_fulltext ( ctx, table ) ) {

			limit = 0;

			for ( uint32_t c = 0; c < table.column_count; c++ ) {

				if ( plan.used [ c ] && static_cast < int32_t > ( c ) != table.rowid_column )
					limit = c + 1;
				else
					partial = true;

			}

			partial = partial || limit < table.column_count;

		}

		source.decode_limit = partial ? limit : RECORD_COLUMNS + 1;
		source.decode_ready = true;

		return source.decode_limit;

	}

	static bool decode_scanned ( run_context& ctx, source_state& source, int64_t rowid, const uint8_t* data, size_t size ) {

		const table_info& table = *source.plan->table;
		uint32_t limit = decode_limit ( ctx, source );

		if ( limit > RECORD_COLUMNS )
			return decode_bytes ( ctx, table, rowid, data, size, source.values );

		storage::value* values = static_cast < storage::value* > ( ctx.scratch->take_raw ( sizeof ( storage::value ) * ( table.column_count + 1 ) ) );
		uint32_t count = 0;

		if ( !values )
			return fail_run ( ctx, "out of memory" );

		values [ table.column_count ] = storage::null_value ( );
		source.values = values;

		if ( !storage::decode_wanted ( data, size, values, table.column_count, source.plan->used, limit, count ) )
			return fail_named ( ctx, "a row of table ", table.name, " is damaged" );

		for ( uint32_t i = count; i < table.column_count; i++ ) {

			if ( i >= limit )
				values [ i ] = storage::null_value ( );
			else if ( !fallback_value ( ctx, table.columns [ i ], values [ i ] ) )
				return false;

		}

		if ( table.rowid_column >= 0 )
			values [ table.rowid_column ] = storage::integer_value ( rowid );

		return true;

	}

	static bool read_source_row ( run_context& ctx, source_state& source, int64_t rowid, bool& found ) {

		storage::btree tree = { ctx.pages, source.plan->table->root, false };
		const uint8_t* data = nullptr;
		size_t size = 0;

		if ( !storage::table_lookup ( tree, rowid, *ctx.scratch, data, size, found ) )
			return fail_storage ( ctx, "a row could not be read" );

		if ( !found )
			return true;

		return decode_scanned ( ctx, source, rowid, data, size );

	}

	static bool quick_side ( const sql::expression* item, int32_t level ) {

		return item && item->kind == sql::EXPRESSION_COLUMN && item->depth == 0 && item->source == level && !item->nocase;

	}

	static bool quick_constant ( const sql::expression* item ) {

		return item && ( item->kind == sql::EXPRESSION_LITERAL || item->kind == sql::EXPRESSION_PARAMETER ) && !item->nocase;

	}

	static bool quick_operator ( sql::operator_kind op ) {

		return op == sql::OPERATOR_EQUAL || op == sql::OPERATOR_NOT_EQUAL || op == sql::OPERATOR_LESS || op == sql::OPERATOR_LESS_EQUAL || op == sql::OPERATOR_GREATER || op == sql::OPERATOR_GREATER_EQUAL || op == sql::OPERATOR_IS || op == sql::OPERATOR_IS_NOT;

	}

	static uint8_t ascii_lower ( uint8_t value ) {

		return value >= 'A' && value <= 'Z' ? static_cast < uint8_t > ( value + 32 ) : value;

	}

	static void shape_like ( base::arena& memory, quick_test& test ) {

		const uint8_t* pattern = test.constant.bytes;
		size_t size = test.constant.size;

		test.shape = LIKE_GENERIC;

		if ( test.glob || test.constant.kind != storage::VALUE_TEXT )
			return;

		size_t start = 0;
		size_t end = size;
		bool lead = false;
		bool tail = false;

		while ( start < end && pattern [ start ] == '%' ) {

			start++;
			lead = true;

		}

		while ( end > start && pattern [ end - 1 ] == '%' ) {

			end--;
			tail = true;

		}

		for ( size_t i = start; i < end; i++ )
			if ( pattern [ i ] == '%' || pattern [ i ] == '_' )
				return;

		uint8_t* folded = static_cast < uint8_t* > ( memory.take_raw ( end - start + 1 ) );

		if ( !folded )
			return;

		for ( size_t i = start; i < end; i++ )
			folded [ i - start ] = ascii_lower ( pattern [ i ] );

		test.literal = folded;
		test.literal_size = end - start;
		test.shape = start == end ? ( lead || tail ? LIKE_ANYTHING : LIKE_EXACT ) : lead && tail ? LIKE_CONTAINS : lead ? LIKE_SUFFIX : tail ? LIKE_PREFIX : LIKE_EXACT;

	}

	static bool folded_equal ( const uint8_t* text, const uint8_t* literal, size_t size ) {

		for ( size_t i = 0; i < size; i++ )
			if ( ascii_lower ( text [ i ] ) != literal [ i ] )
				return false;

		return true;

	}

	static bool like_fast ( run_context& ctx, const quick_test& test, const storage::value& value ) {

		storage::value text = value.kind == storage::VALUE_TEXT || value.kind == storage::VALUE_BLOB ? value : text_of ( *ctx.scratch, value );
		const uint8_t* bytes = text.bytes;
		size_t size = text.size;
		const uint8_t* literal = test.literal;
		size_t wanted = test.literal_size;

		switch ( test.shape ) {

			case LIKE_ANYTHING:
				return true;

			case LIKE_EXACT:
				return size == wanted && folded_equal ( bytes, literal, wanted );

			case LIKE_PREFIX:
				return size >= wanted && folded_equal ( bytes, literal, wanted );

			case LIKE_SUFFIX:
				return size >= wanted && folded_equal ( bytes + size - wanted, literal, wanted );

			case LIKE_CONTAINS: {

				if ( size < wanted )
					return false;

				uint8_t first = literal [ 0 ];

				for ( size_t i = 0; i + wanted <= size; i++ )
					if ( ascii_lower ( bytes [ i ] ) == first && folded_equal ( bytes + i, literal, wanted ) )
						return true;

				return false;

			}

			default:
				return like_match ( test.constant.bytes, test.constant.size, bytes, size, 0, test.glob );

		}

	}

	void source_filters ( const source_state& source, sql::expression* const*& terms, uint32_t& count ) {

		if ( source.quick_ready ) {

			terms = source.residual;
			count = source.residual_count;

			return;

		}

		terms = source.plan->filters;
		count = source.plan->filter_count - source.plan->path.consumed;

	}

	static bool compile_quick ( run_context& ctx, query_state* scope, source_state& source ) {

		prepared_source& plan = *source.plan;
		const table_info* table = plan.table;

		source.quick_count = 0;
		source.residual_count = 0;
		source.quick_limit = 0;

		if ( !table || plan.left || plan.right || !plan.filter_count || table->generated_count || !scope || !scope->plan || plan.width != table->column_count ) {

			source.quick_ready = false;

			return true;

		}

		int32_t level = static_cast < int32_t > ( &plan - scope->plan->sources );

		if ( level < 0 || static_cast < uint32_t > ( level ) >= scope->plan->source_count ) {

			source.quick_ready = false;

			return true;

		}

		source.quick = ctx.memory->make < quick_test > ( plan.filter_count );
		source.residual = ctx.memory->make < sql::expression* > ( plan.filter_count );
		source.quick_wanted = ctx.memory->make < bool > ( table->column_count + 1 );

		if ( !source.quick || !source.residual || !source.quick_wanted )
			return fail_run ( ctx, "out of memory" );

		for ( uint32_t t = 0; t < plan.filter_count - plan.path.consumed; t++ ) {

			sql::expression* term = plan.filters [ t ];
			const sql::expression* column = nullptr;
			const sql::expression* other = nullptr;
			sql::operator_kind op = term->op;

			bool like = false;

			if ( term->kind == sql::EXPRESSION_BINARY && quick_operator ( op ) ) {

				if ( quick_side ( term->left, level ) && quick_constant ( term->right ) ) {

					column = term->left;
					other = term->right;

				} else if ( quick_side ( term->right, level ) && quick_constant ( term->left ) ) {

					column = term->right;
					other = term->left;
					op = flipped ( op );

				}

			} else if ( term->kind == sql::EXPRESSION_LIKE && !term->third && quick_side ( term->left, level ) && quick_constant ( term->right ) ) {

				column = term->left;
				other = term->right;
				like = true;

			}

			if ( !column ) {

				source.residual [ source.residual_count++ ] = term;

				continue;

			}

			storage::value constant;
			storage::value unused = storage::null_value ( );
			int32_t index = column->column;
			bool rowid = index < 0 || index == table->rowid_column;

			if ( !evaluate ( ctx, scope, other, constant ) )
				return false;

			if ( like ) {

				if ( constant.kind != storage::VALUE_NULL )
					constant = text_of ( *ctx.memory, constant );

			} else
				comparison_affinity ( *ctx.memory, rowid ? sql::AFFINITY_INTEGER : table->columns [ index ].affinity, sql::AFFINITY_ANY, unused, constant );

			constant = keep_value ( *ctx.memory, constant );

			quick_test& made = source.quick [ source.quick_count++ ];

			made = { };
			made.column = rowid ? -1 : index;
			made.op = op;
			made.constant = constant;
			made.like = like;
			made.glob = like && term->star;
			made.negated = like && term->negated;

			if ( like )
				shape_like ( *ctx.memory, made );

			if ( !rowid ) {

				source.quick_wanted [ index ] = true;
				source.quick_limit = base::max < uint32_t > ( source.quick_limit, static_cast < uint32_t > ( index ) + 1 );

			}

		}

		source.quick_column = source.quick_count ? source.quick [ 0 ].column : QUICK_MIXED;

		for ( uint32_t q = 1; q < source.quick_count; q++ )
			if ( source.quick [ q ].column != source.quick_column )
				source.quick_column = QUICK_MIXED;

		source.quick_ready = true;

		return true;

	}

	static bool quick_one ( run_context& ctx, const quick_test& test, const storage::value& value ) {

		if ( test.like )
			return value.kind != storage::VALUE_NULL && test.constant.kind != storage::VALUE_NULL && like_fast ( ctx, test, value ) != test.negated;

		if ( value.kind == storage::VALUE_INTEGER && test.constant.kind == storage::VALUE_INTEGER ) {

			int64_t a = value.integer;
			int64_t b = test.constant.integer;

			switch ( test.op ) {

				case sql::OPERATOR_EQUAL:
				case sql::OPERATOR_IS:
					return a == b;

				case sql::OPERATOR_NOT_EQUAL:
				case sql::OPERATOR_IS_NOT:
					return a != b;

				case sql::OPERATOR_LESS:
					return a < b;

				case sql::OPERATOR_LESS_EQUAL:
					return a <= b;

				case sql::OPERATOR_GREATER:
					return a > b;

				case sql::OPERATOR_GREATER_EQUAL:
					return a >= b;

				default:
					break;

			}

		}

		return compare_with ( test.op, value, test.constant ) == TRUTH_TRUE;

	}

	static bool quick_passes ( run_context& ctx, source_state& source, int64_t rowid, const uint8_t* data, size_t size, bool& pass ) {

		const table_info& table = *source.plan->table;

		if ( source.quick_column != QUICK_MIXED ) {

			storage::value value;

			pass = true;

			if ( source.quick_column < 0 )
				value = storage::integer_value ( rowid );
			else {

				bool present = false;

				if ( !storage::field_at ( data, size, static_cast < uint32_t > ( source.quick_column ), value, present ) )
					return fail_named ( ctx, "a row of table ", table.name, " is damaged" );

				if ( !present && !fallback_value ( ctx, table.columns [ source.quick_column ], value ) )
					return false;

			}

			for ( uint32_t q = 0; q < source.quick_count && pass; q++ )
				pass = quick_one ( ctx, source.quick [ q ], value );

			return true;

		}

		storage::value* values = static_cast < storage::value* > ( ctx.scratch->take_raw ( sizeof ( storage::value ) * ( table.column_count + 1 ) ) );
		uint32_t count = 0;

		pass = true;

		if ( !values )
			return fail_run ( ctx, "out of memory" );

		if ( source.quick_limit && !storage::decode_wanted ( data, size, values, table.column_count, source.quick_wanted, source.quick_limit, count ) )
			return fail_named ( ctx, "a row of table ", table.name, " is damaged" );

		for ( uint32_t q = 0; q < source.quick_count && pass; q++ ) {

			const quick_test& test = source.quick [ q ];
			storage::value value;

			if ( test.column < 0 )
				value = storage::integer_value ( rowid );
			else if ( static_cast < uint32_t > ( test.column ) < count )
				value = values [ test.column ];
			else if ( !fallback_value ( ctx, table.columns [ test.column ], value ) )
				return false;

			pass = quick_one ( ctx, test, value );

		}

		return true;

	}

	static bool sorts_in ( run_context& ctx, source_state& source, int64_t rowid, const uint8_t* data, size_t size, bool& pass ) {

		const table_info& table = *source.plan->table;
		storage::value key;

		pass = true;

		if ( source.early_pipe->row_count < static_cast < uint64_t > ( source.early_pipe->bound ) )
			return true;

		if ( source.early_column < 0 || source.early_column == table.rowid_column )
			key = storage::integer_value ( rowid );
		else {

			bool present = false;

			if ( !storage::field_at ( data, size, static_cast < uint32_t > ( source.early_column ), key, present ) )
				return fail_named ( ctx, "a row of table ", table.name, " is damaged" );

			if ( !present && !fallback_value ( ctx, table.columns [ source.early_column ], key ) )
				return false;

		}

		pass = !sort_rejects ( *source.early_pipe, key );

		return true;

	}

	bool read_row ( run_context& ctx, const table_info& table, int64_t rowid, storage::value*& values, bool& found ) {

		storage::btree tree = { ctx.pages, table.root, false };
		const uint8_t* data = nullptr;
		size_t size = 0;

		if ( !storage::table_lookup ( tree, rowid, *ctx.scratch, data, size, found ) )
			return fail_storage ( ctx, "a row could not be read" );

		if ( !found )
			return true;

		return decode_bytes ( ctx, table, rowid, data, size, values );

	}

	bool make_index_key ( run_context& ctx, const index_info& index, const storage::value* values, int64_t rowid, bool with_rowid, base::text& out ) {

		storage::value key [ RUN_KEY_ROOM ];

		for ( uint32_t k = 0; k < index.column_count; k++ )
			key [ k ] = values [ index.columns [ k ] ];

		key [ index.column_count ] = storage::integer_value ( rowid );
		out.clear ( );
		storage::encode_record ( key, index.column_count + ( with_rowid ? 1 : 0 ), out );

		return true;

	}

	static storage::value number_from ( const storage::value& item ) {

		if ( item.kind != storage::VALUE_TEXT )
			return item;

		storage::value parsed;

		return read_number_text ( item.bytes, item.size, parsed, true ) ? parsed : item;

	}

	bool exact_rowid ( const storage::value& item, int64_t& out ) {

		storage::value number = number_from ( item );

		if ( number.kind == storage::VALUE_INTEGER ) {

			out = number.integer;

			return true;

		}

		if ( number.kind == storage::VALUE_REAL && number.real >= -9.2e18 && number.real <= 9.2e18 && number.real == static_cast < double > ( static_cast < int64_t > ( number.real ) ) ) {

			out = static_cast < int64_t > ( number.real );

			return true;

		}

		return false;

	}

	static bool bound_rowid ( const storage::value& item, bool lower, bool inclusive, int64_t& out, bool& empty, bool& open ) {

		storage::value number = number_from ( item );

		open = false;

		if ( number.kind == storage::VALUE_NULL ) {

			empty = true;

			return true;

		}

		if ( number.kind == storage::VALUE_TEXT || number.kind == storage::VALUE_BLOB ) {

			if ( lower )
				empty = true;
			else
				open = true;

			return true;

		}

		static const int64_t highest = 0x7FFFFFFFFFFFFFFFll;
		static const int64_t lowest = static_cast < int64_t > ( 0x8000000000000000ull );

		if ( number.kind == storage::VALUE_REAL ) {

			double real = number.real;

			if ( real >= 9.2e18 ) {

				if ( lower )
					empty = true;
				else
					open = true;

				return true;

			}

			if ( real <= -9.2e18 ) {

				if ( lower )
					open = true;
				else
					empty = true;

				return true;

			}

			int64_t whole = static_cast < int64_t > ( real );

			if ( static_cast < double > ( whole ) == real ) {

				number = storage::integer_value ( whole );

			} else if ( lower ) {

				out = real > 0 ? whole + 1 : whole;

				return true;

			} else {

				out = real > 0 ? whole : whole - 1;

				return true;

			}

		}

		int64_t value = number.integer;

		if ( lower ) {

			if ( !inclusive ) {

				if ( value == highest ) {

					empty = true;

					return true;

				}

				value++;

			}

		} else if ( !inclusive ) {

			if ( value == lowest ) {

				empty = true;

				return true;

			}

			value--;

		}

		out = value;

		return true;

	}

	static void sort_rowids ( int64_t* items, uint32_t count ) {

		for ( uint32_t gap = count / 2; gap; gap /= 2 ) {

			for ( uint32_t i = gap; i < count; i++ ) {

				int64_t here = items [ i ];
				uint32_t k = i;

				while ( k >= gap && items [ k - gap ] > here ) {

					items [ k ] = items [ k - gap ];
					k -= gap;

				}

				items [ k ] = here;

			}

		}

	}

	static bool keep_key ( run_context& ctx, const storage::value* values, uint32_t count, const uint8_t*& out, size_t& size ) {

		size = storage::record_size ( values, count );

		uint8_t* kept = static_cast < uint8_t* > ( ctx.scratch->take_raw ( size + 1 ) );

		if ( !kept )
			return fail_run ( ctx, "out of memory" );

		storage::write_record ( values, count, kept );
		out = kept;

		return true;

	}

	static storage::key_probe* keep_probe ( run_context& ctx, const uint8_t* key, size_t size, uint32_t columns ) {

		storage::key_probe* made = static_cast < storage::key_probe* > ( ctx.scratch->take_raw ( sizeof ( storage::key_probe ) ) );

		if ( made )
			storage::make_probe ( key, size, columns, *made );

		return made;

	}

	static int32_t order_entry ( const uint8_t* entry, size_t size, const storage::key_probe* probe, const uint8_t* key, size_t key_size, uint32_t columns ) {

		return probe ? storage::compare_probe ( entry, size, *probe ) : storage::compare_records ( entry, size, key, key_size, columns );

	}

	static bool open_index ( run_context& ctx, query_state* scope, source_state& source ) {

		const access_path& path = source.plan->path;
		const index_info& index = *path.index;
		const table_info& table = *source.plan->table;
		storage::value key [ RUN_KEY_ROOM ];
		uint32_t equal = path.equal_count;

		source.index_tree = { ctx.pages, index.root, true };

		for ( uint32_t k = 0; k < equal; k++ ) {

			storage::value value;

			if ( !evaluate ( ctx, scope, path.equal [ k ], value ) )
				return false;

			key [ k ] = seek_value ( *ctx.scratch, table.columns [ index.columns [ k ] ].affinity, value );

			if ( key [ k ].kind == storage::VALUE_NULL ) {

				source.done = true;

				return true;

			}

		}

		if ( !keep_key ( ctx, key, equal, source.equal_key, source.equal_size ) )
			return false;

		source.equal_probe = equal ? keep_probe ( ctx, source.equal_key, source.equal_size, equal ) : nullptr;
		source.high_probe = nullptr;

		sql::affinity_kind range_affinity = equal < index.column_count ? table.columns [ index.columns [ equal ] ].affinity : sql::AFFINITY_ANY;

		source.high_key = nullptr;
		source.low_key = nullptr;

		if ( path.high ) {

			storage::value value;

			if ( !evaluate ( ctx, scope, path.high, value ) )
				return false;

			key [ equal ] = seek_value ( *ctx.scratch, range_affinity, value );

			if ( key [ equal ].kind == storage::VALUE_NULL ) {

				source.done = true;

				return true;

			}

			if ( !keep_key ( ctx, key, equal + 1, source.high_key, source.high_size ) )
				return false;

			source.high_probe = keep_probe ( ctx, source.high_key, source.high_size, equal + 1 );
			source.high_columns = equal + 1;
			source.high_inclusive = path.high_inclusive;

		}

		if ( path.low ) {

			storage::value value;

			if ( !evaluate ( ctx, scope, path.low, value ) )
				return false;

			key [ equal ] = seek_value ( *ctx.scratch, range_affinity, value );

			if ( key [ equal ].kind == storage::VALUE_NULL ) {

				source.done = true;

				return true;

			}

			if ( !keep_key ( ctx, key, equal + 1, source.low_key, source.low_size ) )
				return false;

			source.low_exclusive = !path.low_inclusive;

			if ( !storage::cursor_seek_key ( source.index_at, source.index_tree, source.low_key, source.low_size, equal + 1 ) )
				return fail_storage ( ctx, "an index could not be searched" );

			return true;

		}

		if ( !equal ) {

			if ( !storage::cursor_first ( source.index_at, source.index_tree ) )
				return fail_storage ( ctx, "an index could not be read" );

			return true;

		}

		if ( !storage::cursor_seek_key ( source.index_at, source.index_tree, source.equal_key, source.equal_size, equal ) )
			return fail_storage ( ctx, "an index could not be searched" );

		if ( source.index_at.match == storage::SEEK_NONE )
			source.done = true;

		return true;

	}

	bool open_source ( run_context& ctx, query_state* scope, source_state& source ) {

		prepared_source& plan = *source.plan;

		source.null_row = false;
		source.done = false;
		source.first = true;
		source.values = nullptr;

		if ( plan.working ) {

			source.rows = plan.cte->working;
			source.row_count = plan.cte->working_count;
			source.row_at = 0;

			return true;

		}

		if ( plan.query ) {

			if ( !source.materialized ) {

				collected_rows rows = { };

				rows.memory = ctx.memory;

				if ( plan.cte && plan.cte->recursive ? !collect_recursive ( ctx, *plan.cte, scope->parent, rows ) : !collect_plan ( ctx, plan.query, scope->parent, rows ) )
					return false;

				source.rows = rows.values;
				source.row_count = rows.count;
				source.materialized = true;

			}

			source.row_at = 0;

			return true;

		}

		source.tree = { ctx.pages, plan.table->root, false };

		switch ( plan.path.kind ) {

			case PATH_SCAN:
				if ( !storage::cursor_first ( source.at, source.tree ) )
					return fail_storage ( ctx, "a table could not be read" );

				return true;

			case PATH_ROWID_EQUAL: {

				storage::value value;

				if ( !evaluate ( ctx, scope, plan.path.equal [ 0 ], value ) )
					return false;

				if ( !exact_rowid ( value, source.low ) )
					source.done = true;

				source.at.valid = false;

				return true;

			}

			case PATH_ROWID_RANGE: {

				bool empty = false;
				bool open = false;

				source.low = static_cast < int64_t > ( 0x8000000000000000ull );
				source.high = 0x7FFFFFFFFFFFFFFFll;

				if ( plan.path.low ) {

					storage::value value;

					if ( !evaluate ( ctx, scope, plan.path.low, value ) || !bound_rowid ( value, true, plan.path.low_inclusive, source.low, empty, open ) )
						return false;

					if ( open )
						source.low = static_cast < int64_t > ( 0x8000000000000000ull );

				}

				if ( !empty && plan.path.high ) {

					storage::value value;

					if ( !evaluate ( ctx, scope, plan.path.high, value ) || !bound_rowid ( value, false, plan.path.high_inclusive, source.high, empty, open ) )
						return false;

					if ( open )
						source.high = 0x7FFFFFFFFFFFFFFFll;

				}

				if ( empty || source.low > source.high ) {

					source.done = true;

					return true;

				}

				if ( !storage::cursor_seek_rowid ( source.at, source.tree, source.low ) )
					return fail_storage ( ctx, "a table could not be searched" );

				return true;

			}

			case PATH_ROWID_LIST: {

				const sql::expression* list = plan.path.list;
				collected_rows found = { };
				uint32_t room = list->count;
				base::arena_mark before = ctx.memory->mark ( );

				if ( list->query ) {

					found.memory = ctx.memory;

					if ( !collect_plan ( ctx, static_cast < prepared_select* > ( list->query->prepared ), scope, found ) )
						return false;

					room = found.count;

				}

				source.list = ctx.scratch->make < int64_t > ( room + 1 );
				source.list_count = 0;
				source.list_at = 0;

				if ( !source.list ) {

					ctx.memory->rewind ( before );

					return fail_run ( ctx, "out of memory" );

				}

				for ( uint32_t i = 0; i < room; i++ ) {

					storage::value value;
					int64_t rowid = 0;

					if ( list->query )
						value = found.values [ static_cast < size_t > ( i ) * found.width ];
					else if ( !evaluate ( ctx, scope, list->list [ i ], value ) )
						return false;

					if ( exact_rowid ( value, rowid ) )
						source.list [ source.list_count++ ] = rowid;

				}

				if ( list->query )
					ctx.memory->rewind ( before );

				sort_rowids ( source.list, source.list_count );

				uint32_t unique = 0;

				for ( uint32_t i = 0; i < source.list_count; i++ )
					if ( !unique || source.list [ unique - 1 ] != source.list [ i ] )
						source.list [ unique++ ] = source.list [ i ];

				source.list_count = unique;

				return true;

			}

			case PATH_INDEX:
				return open_index ( ctx, scope, source );

			case PATH_FULLTEXT: {

				search_entry* entry = nullptr;

				if ( !search_lookup ( ctx, scope, plan.path.list, entry ) )
					return false;

				source.list = ctx.scratch->make < int64_t > ( entry->result.count + 1 );

				if ( !source.list )
					return fail_run ( ctx, "out of memory" );

				for ( uint32_t i = 0; i < entry->result.count; i++ )
					source.list [ i ] = entry->result.hits [ i ].rowid;

				source.list_count = entry->result.count;
				source.list_at = 0;

				return true;

			}

		}

		return true;

	}

	static bool covered_row ( run_context& ctx, source_state& source, const uint8_t* entry, size_t size, int64_t rowid, bool& found ) {

		const index_info& index = *source.plan->path.index;
		const table_info& table = *source.plan->table;
		char* kept = ctx.scratch->keep ( reinterpret_cast < const char* > ( entry ), size );
		storage::value* values = ctx.scratch->make < storage::value > ( table.column_count + 1 );
		storage::value key [ RUN_KEY_ROOM ];
		uint32_t count = 0;

		if ( !kept || !values )
			return fail_run ( ctx, "out of memory" );

		if ( !storage::decode_record ( reinterpret_cast < const uint8_t* > ( kept ), size, key, RUN_KEY_ROOM, count ) || count != index.column_count + 1 )
			return fail_named ( ctx, "an entry of index ", index.name, " is damaged" );

		for ( uint32_t c = 0; c < table.column_count; c++ )
			values [ c ] = storage::null_value ( );

		for ( uint32_t k = 0; k < index.column_count; k++ )
			values [ index.columns [ k ] ] = key [ k ];

		if ( table.rowid_column >= 0 )
			values [ table.rowid_column ] = storage::integer_value ( rowid );

		source.values = values;
		source.rowid = rowid;
		found = true;

		return true;

	}

	static bool next_index ( run_context& ctx, source_state& source, bool& found ) {

		const access_path& path = source.plan->path;
		uint32_t equal = path.equal_count;

		while ( source.index_at.valid ) {

			int64_t unused = 0;
			const uint8_t* entry = nullptr;
			size_t size = 0;
			storage::page_frame* held = nullptr;

			if ( !storage::cursor_view ( source.index_at, *ctx.scratch, unused, entry, size, held ) )
				return fail_storage ( ctx, "an index could not be read" );

			bool stop = equal && order_entry ( entry, size, source.equal_probe, source.equal_key, source.equal_size, equal ) != 0;

			if ( !stop && source.high_key ) {

				int32_t order = order_entry ( entry, size, source.high_probe, source.high_key, source.high_size, source.high_columns );

				stop = order > 0 || ( order == 0 && !source.high_inclusive );

			}

			bool skip = !stop && source.low_key && source.low_exclusive && storage::compare_records ( entry, size, source.low_key, source.low_size, equal + 1 ) == 0;
			int64_t rowid = 0;
			bool damaged = !stop && !skip && !storage::last_integer ( entry, size, rowid );

			if ( stop || skip || damaged || !path.covering ) {

				if ( held )
					storage::release_page ( held );

				if ( stop ) {

					source.done = true;

					return true;

				}

				if ( damaged )
					return fail_named ( ctx, "an entry of index ", path.index->name, " is damaged" );

				if ( skip )
					continue;

			} else {

				bool fine = covered_row ( ctx, source, entry, size, rowid, found );

				if ( held )
					storage::release_page ( held );

				return fine;

			}

			bool present = false;

			if ( !read_source_row ( ctx, source, rowid, present ) )
				return false;

			if ( !present )
				return fail_named ( ctx, "index ", path.index->name, " points at a row that is gone" );

			source.rowid = rowid;
			found = true;

			return true;

		}

		source.done = true;

		return true;

	}

	static storage::scan_outcome scan_check ( void* context, int64_t rowid, const uint8_t* data, size_t size ) {

		scan_filter& item = *static_cast < scan_filter* > ( context );
		run_context& ctx = *item.ctx;
		source_state& source = *item.source;
		bool pass = true;

		if ( item.range && rowid > item.high )
			return storage::SCAN_STOP;

		if ( item.checked ) {

			bool mixed = source.quick_column == QUICK_MIXED;
			base::arena_mark mark = { };

			if ( mixed )
				mark = ctx.scratch->mark ( );

			if ( !quick_passes ( ctx, source, rowid, data, size, pass ) ) {

				item.failed = true;

				return storage::SCAN_FAIL;

			}

			if ( !pass ) {

				if ( mixed )
					ctx.scratch->rewind ( mark );

				return storage::SCAN_REJECT;

			}

		}

		if ( item.early ) {

			if ( !sorts_in ( ctx, source, rowid, data, size, pass ) ) {

				item.failed = true;

				return storage::SCAN_FAIL;

			}

			if ( !pass )
				return storage::SCAN_REJECT;

		}

		return storage::SCAN_TAKE;

	}

	static bool next_filtered ( run_context& ctx, source_state& source, bool checked, bool early, bool& found ) {

		scan_filter filter = { &ctx, &source, checked, early, source.plan->path.kind == PATH_ROWID_RANGE, false, source.high };
		storage::scan_result result;

		if ( !storage::cursor_scan ( source.at, *ctx.scratch, scan_check, &filter, result ) )
			return fail_storage ( ctx, "a table could not be read" );

		if ( result.examined > 1 )
			ctx.rows_examined += result.examined - 1;

		if ( result.outcome == storage::SCAN_FAIL )
			return false;

		if ( result.outcome != storage::SCAN_TAKE ) {

			source.done = true;

			return true;

		}

		source.row_page = result.page;
		source.row_slot = result.slot;
		source.rowid = result.rowid;
		found = true;

		return decode_scanned ( ctx, source, result.rowid, result.data, result.size );

	}

	static bool next_rowid ( run_context& ctx, query_state* scope, source_state& source, bool& found ) {

		if ( !source.first ) {

			source.done = true;

			return true;

		}

		source.first = false;

		if ( !source.quick_compiled ) {

			source.quick_compiled = true;

			if ( !compile_quick ( ctx, scope, source ) )
				return false;

		}

		const uint8_t* data = nullptr;
		size_t size = 0;
		bool present = false;
		uint32_t page = 0;
		uint32_t slot = 0;

		if ( !storage::table_lookup ( source.tree, source.low, *ctx.scratch, data, size, present, &page, &slot ) )
			return fail_storage ( ctx, "a table could not be searched" );

		source.done = true;

		if ( !present )
			return true;

		if ( source.quick_ready && source.quick_count ) {

			bool pass = true;

			if ( !quick_passes ( ctx, source, source.low, data, size, pass ) )
				return false;

			if ( !pass )
				return true;

		}

		source.done = false;
		source.row_page = page;
		source.row_slot = slot;
		source.rowid = source.low;
		found = true;

		return decode_scanned ( ctx, source, source.low, data, size );

	}

	bool next_source ( run_context& ctx, query_state* scope, source_state& source, bool& found ) {

		found = false;

		if ( source.done )
			return true;

		prepared_source& plan = *source.plan;

		if ( plan.query || plan.working ) {

			if ( source.row_at >= source.row_count ) {

				source.done = true;

				return true;

			}

			source.values = source.rows + static_cast < size_t > ( source.row_at ) * plan.width;
			source.row_at++;
			found = true;

			return true;

		}

		ctx.rows_examined++;

		switch ( plan.path.kind ) {

			case PATH_ROWID_EQUAL:
				return next_rowid ( ctx, scope, source, found );

			case PATH_SCAN:
			case PATH_ROWID_RANGE: {

				if ( !source.at.valid ) {

					source.done = true;

					return true;

				}

				source.first = false;

				if ( !source.quick_compiled ) {

					source.quick_compiled = true;

					if ( !compile_quick ( ctx, scope, source ) )
						return false;

				}

				bool checked = source.quick_ready && source.quick_count;
				bool early = source.early_pipe && ( source.quick_ready ? !source.residual_count : !plan.filter_count );

				if ( checked || early )
					return next_filtered ( ctx, source, checked, early, found );

				int64_t rowid = 0;
				const uint8_t* data = nullptr;
				size_t size = 0;
				storage::page_frame* held = nullptr;
				uint32_t row_page = source.at.page;
				uint32_t row_slot = source.at.slot;

				if ( !storage::cursor_view ( source.at, *ctx.scratch, rowid, data, size, held ) )
					return fail_storage ( ctx, "a table could not be read" );

				if ( plan.path.kind == PATH_ROWID_RANGE && rowid > source.high ) {

					if ( held )
						storage::release_page ( held );

					source.done = true;

					return true;

				}

				if ( held ) {

					uint8_t* kept = static_cast < uint8_t* > ( ctx.scratch->take_raw ( size + 1 ) );

					if ( kept )
						base::copy ( kept, data, size );

					storage::release_page ( held );

					if ( !kept )
						return fail_run ( ctx, "out of memory" );

					data = kept;

				}

				source.row_page = row_page;
				source.row_slot = row_slot;
				source.rowid = rowid;
				found = true;

				return decode_scanned ( ctx, source, rowid, data, size );

			}

			case PATH_ROWID_LIST:
			case PATH_FULLTEXT:
				while ( source.list_at < source.list_count ) {

					int64_t rowid = source.list [ source.list_at++ ];
					bool present = false;

					if ( !read_source_row ( ctx, source, rowid, present ) )
						return false;

					if ( present ) {

						source.rowid = rowid;
						found = true;

						return true;

					}

				}

				source.done = true;

				return true;

			case PATH_INDEX:
				return next_index ( ctx, source, found );

		}

		source.done = true;

		return true;

	}

}
