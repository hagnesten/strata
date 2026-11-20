#include "engine/search/search.hh"
#include "engine/value/interval.hh"

namespace engine {

	static bool numeric_affinity ( sql::affinity_kind affinity ) {

		return affinity == sql::AFFINITY_INTEGER || affinity == sql::AFFINITY_REAL || affinity == sql::AFFINITY_NUMERIC;

	}

	void comparison_affinity ( base::arena& memory, sql::affinity_kind left, sql::affinity_kind right, storage::value& a, storage::value& b ) {

		if ( numeric_affinity ( left ) && !numeric_affinity ( right ) ) {

			if ( b.kind == storage::VALUE_TEXT || b.kind == storage::VALUE_REAL )
				b = apply_affinity ( memory, b, sql::AFFINITY_NUMERIC );

		} else if ( numeric_affinity ( right ) && !numeric_affinity ( left ) ) {

			if ( a.kind == storage::VALUE_TEXT || a.kind == storage::VALUE_REAL )
				a = apply_affinity ( memory, a, sql::AFFINITY_NUMERIC );

		} else if ( left == sql::AFFINITY_TEXT && right == sql::AFFINITY_ANY ) {

			if ( b.kind == storage::VALUE_INTEGER || b.kind == storage::VALUE_REAL || b.kind == storage::VALUE_DECIMAL )
				b = apply_affinity ( memory, b, sql::AFFINITY_TEXT );

		} else if ( right == sql::AFFINITY_TEXT && left == sql::AFFINITY_ANY ) {

			if ( a.kind == storage::VALUE_INTEGER || a.kind == storage::VALUE_REAL || a.kind == storage::VALUE_DECIMAL )
				a = apply_affinity ( memory, a, sql::AFFINITY_TEXT );

		}

	}

	storage::value seek_value ( base::arena& memory, sql::affinity_kind affinity, const storage::value& item ) {

		storage::value column = storage::null_value ( );
		storage::value other = item;

		comparison_affinity ( memory, affinity, sql::AFFINITY_ANY, column, other );

		return other;

	}

	bool condition ( run_context& ctx, query_state* scope, const sql::expression* item, truth_kind& out ) {

		storage::value result;

		if ( !evaluate ( ctx, scope, item, result ) )
			return false;

		out = truth_of ( result );

		return true;

	}

	bool passes ( run_context& ctx, query_state* scope, sql::expression* const* terms, uint32_t count, bool& out ) {

		out = true;

		for ( uint32_t i = 0; i < count; i++ ) {

			truth_kind truth = TRUTH_NULL;

			if ( !condition ( ctx, scope, terms [ i ], truth ) )
				return false;

			if ( truth != TRUTH_TRUE ) {

				out = false;

				return true;

			}

		}

		return true;

	}

	static int32_t text_truth ( const char* text, size_t size ) {

		while ( size && ( *text == ' ' || *text == '\t' ) ) {

			text++;
			size--;

		}

		while ( size && ( text [ size - 1 ] == ' ' || text [ size - 1 ] == '\t' ) )
			size--;

		static const char* const yes [ ] = { "t", "true", "y", "yes", "on", "1" };
		static const char* const no [ ] = { "f", "false", "n", "no", "off", "0" };

		for ( const char* word : yes )
			if ( base::length ( word ) == size && base::same_folded_size ( text, word, size ) )
				return 1;

		for ( const char* word : no )
			if ( base::length ( word ) == size && base::same_folded_size ( text, word, size ) )
				return 0;

		return -1;

	}

	static truth_kind negate ( truth_kind truth ) {

		return truth == TRUTH_NULL ? TRUTH_NULL : truth == TRUTH_TRUE ? TRUTH_FALSE : TRUTH_TRUE;

	}

	static truth_kind both ( truth_kind a, truth_kind b ) {

		if ( a == TRUTH_FALSE || b == TRUTH_FALSE )
			return TRUTH_FALSE;

		if ( a == TRUTH_NULL || b == TRUTH_NULL )
			return TRUTH_NULL;

		return TRUTH_TRUE;

	}

	static bool interval_of ( const storage::value& item, interval_parts& out ) {

		return item.kind == storage::VALUE_TEXT && parse_interval ( item.bytes, item.size, out );

	}

	static bool moment_of ( const storage::value& item, moment_parts& out ) {

		out = { };

		return item.kind == storage::VALUE_TEXT && item.size >= 8 && parse_moment ( item.bytes, item.size, true, out ) && out.has_date;

	}

	static bool interval_text ( run_context& ctx, const interval_parts& parts, storage::value& out ) {

		base::text written;

		write_interval ( parts, written );

		char* kept = ctx.scratch->keep ( written.value ( ), written.size );

		if ( !kept )
			return fail_run ( ctx, "out of memory" );

		out = storage::text_value ( kept, written.size );
		out.subtype = LITERAL_INTERVAL;

		return true;

	}

	static bool moment_text ( run_context& ctx, const moment_parts& parts, bool date_only, storage::value& out ) {

		base::text written;

		write_moment ( parts, date_only ? MOMENT_DATE : MOMENT_TIMESTAMP, written );

		char* kept = ctx.scratch->keep ( written.value ( ), written.size );

		if ( !kept )
			return fail_run ( ctx, "out of memory" );

		out = storage::text_value ( kept, written.size );

		return true;

	}

	static bool moment_arithmetic ( run_context& ctx, sql::operator_kind op, const storage::value& a, const storage::value& b, storage::value& out, bool& handled ) {

		handled = false;

		if ( !ctx.schema->postgres || a.kind == storage::VALUE_NULL || b.kind == storage::VALUE_NULL )
			return true;

		bool a_interval = a.subtype == LITERAL_INTERVAL && a.kind == storage::VALUE_TEXT;
		bool b_interval = b.subtype == LITERAL_INTERVAL && b.kind == storage::VALUE_TEXT;
		bool a_text = a.kind == storage::VALUE_TEXT;
		bool b_text = b.kind == storage::VALUE_TEXT;

		if ( op == sql::OPERATOR_ADD || op == sql::OPERATOR_SUBTRACT ) {

			moment_parts moment = { };
			moment_parts other = { };
			interval_parts by = { };

			if ( a_text && !a_interval && moment_of ( a, moment ) ) {

				if ( ( b_interval || ( b_text && !moment_of ( b, other ) ) ) && interval_of ( b, by ) ) {

					handled = true;
					shift_moment ( moment, by, op == sql::OPERATOR_ADD ? 1 : -1 );

					return moment_text ( ctx, moment, false, out );

				}

				if ( b_text && !b_interval && op == sql::OPERATOR_SUBTRACT && moment_of ( b, other ) ) {

					handled = true;

					if ( !moment.has_time && !other.has_time ) {

						out = storage::integer_value ( moment.days - other.days );

						return true;

					}

					return interval_text ( ctx, moment_gap ( moment, other ), out );

				}

				int64_t whole = 0;

				if ( !moment.has_time && ( b.kind == storage::VALUE_INTEGER || b.kind == storage::VALUE_DECIMAL ) && integer_of ( b, whole ) ) {

					handled = true;
					moment.days += op == sql::OPERATOR_ADD ? whole : -whole;

					return moment_text ( ctx, moment, true, out );

				}

				return true;

			}

			if ( a_interval && op == sql::OPERATOR_ADD && b_text && !b_interval && moment_of ( b, moment ) && interval_of ( a, by ) ) {

				handled = true;
				shift_moment ( moment, by, 1 );

				return moment_text ( ctx, moment, false, out );

			}

			interval_parts left = { };
			interval_parts right = { };

			if ( ( a_interval || b_interval ) && interval_of ( a, left ) && interval_of ( b, right ) ) {

				int64_t sign = op == sql::OPERATOR_ADD ? 1 : -1;

				handled = true;
				left.months += sign * right.months;
				left.days += sign * right.days;
				left.micros += sign * right.micros;

				return interval_text ( ctx, left, out );

			}

			return true;

		}

		if ( op == sql::OPERATOR_MULTIPLY || op == sql::OPERATOR_DIVIDE ) {

			interval_parts parts = { };
			const storage::value& factor = a_interval ? b : a;

			if ( ( !a_interval && !b_interval ) || ( op == sql::OPERATOR_DIVIDE && !a_interval ) || !interval_of ( a_interval ? a : b, parts ) )
				return true;

			double number = 0;

			if ( !real_of ( factor, number ) )
				return true;

			handled = true;

			if ( op == sql::OPERATOR_DIVIDE ) {

				if ( number == 0 )
					return fail_run ( ctx, "division by zero" );

				number = 1.0 / number;

			}

			return interval_text ( ctx, scale_interval ( parts, number ), out );

		}

		return true;

	}

	static truth_kind compare_expressions ( run_context& ctx, sql::operator_kind op, const sql::expression* left, storage::value a, const sql::expression* right, storage::value b ) {

		if ( ( a.subtype == LITERAL_INTERVAL || b.subtype == LITERAL_INTERVAL ) && a.kind == storage::VALUE_TEXT && b.kind == storage::VALUE_TEXT ) {

			interval_parts x = { };
			interval_parts y = { };

			if ( interval_of ( a, x ) && interval_of ( b, y ) )
				return compare_with ( op, storage::integer_value ( interval_key ( x ) ), storage::integer_value ( interval_key ( y ) ), false );

		}

		comparison_affinity ( *ctx.scratch, expression_affinity ( left ), expression_affinity ( right ), a, b );

		return compare_with ( op, a, b, ( left && left->nocase ) || ( right && right->nocase ) );

	}

	static bool cast_value ( run_context& ctx, const storage::value& item, sql::affinity_kind affinity, storage::value& out ) {

		if ( item.kind == storage::VALUE_NULL ) {

			out = item;

			return true;

		}

		if ( ctx.schema->postgres && item.kind == storage::VALUE_TEXT && ( affinity == sql::AFFINITY_INTEGER || affinity == sql::AFFINITY_REAL || affinity == sql::AFFINITY_NUMERIC ) ) {

			storage::value parsed;

			if ( !read_number_text ( item.bytes, item.size, parsed, true ) || ( affinity == sql::AFFINITY_INTEGER && parsed.kind != storage::VALUE_INTEGER ) ) {

				base::text message;

				message.add ( "invalid input syntax for type " ).add ( affinity == sql::AFFINITY_INTEGER ? "integer" : "numeric" ).add ( ": \"" ).add ( reinterpret_cast < const char* > ( item.bytes ), base::min < size_t > ( item.size, 64 ) ).add ( '"' );

				return fail_run ( ctx, message.value ( ) );

			}

		}

		switch ( affinity ) {

			case sql::AFFINITY_INTEGER: {

				int64_t number = 0;

				if ( item.kind == storage::VALUE_TEXT || item.kind == storage::VALUE_BLOB ) {

					storage::value parsed;

					read_number_text ( item.bytes, item.size, parsed, false );
					integer_of ( parsed, number );

				} else
					integer_of ( item, number );

				out = storage::integer_value ( number );

				return true;

			}

			case sql::AFFINITY_REAL: {

				double number = 0;

				if ( item.kind == storage::VALUE_TEXT || item.kind == storage::VALUE_BLOB ) {

					storage::value parsed;

					read_number_text ( item.bytes, item.size, parsed, false );
					real_of ( parsed, number );

				} else
					real_of ( item, number );

				out = storage::real_value ( number );

				return true;

			}

			case sql::AFFINITY_NUMERIC: {

				storage::value parsed = item;

				if ( item.kind == storage::VALUE_TEXT || item.kind == storage::VALUE_BLOB )
					read_number_text ( item.bytes, item.size, parsed, false );

				out = apply_affinity ( *ctx.scratch, parsed, sql::AFFINITY_NUMERIC );

				return true;

			}

			case sql::AFFINITY_TEXT:
				out = text_of ( *ctx.scratch, item );

				if ( item.kind == storage::VALUE_BLOB )
					out.kind = storage::VALUE_TEXT;

				return true;

			default: {

				storage::value text = text_of ( *ctx.scratch, item );

				out = storage::blob_value ( text.bytes, text.size );

				return true;

			}

		}

	}

	static bool concatenate ( run_context& ctx, const storage::value& a, const storage::value& b, storage::value& out ) {

		if ( a.kind == storage::VALUE_NULL || b.kind == storage::VALUE_NULL ) {

			out = storage::null_value ( );

			return true;

		}

		storage::value x = text_of ( *ctx.scratch, a );
		storage::value y = text_of ( *ctx.scratch, b );
		char* joined = ctx.scratch->make < char > ( x.size + y.size + 1 );

		if ( !joined )
			return fail_run ( ctx, "out of memory" );

		if ( x.size )
			base::copy ( joined, x.bytes, x.size );

		if ( y.size )
			base::copy ( joined + x.size, y.bytes, y.size );

		out = storage::text_value ( joined, x.size + y.size );

		return true;

	}

	static bool evaluate_in_list ( run_context& ctx, query_state* scope, const sql::expression* item, storage::value& out ) {

		storage::value probe;

		if ( !evaluate ( ctx, scope, item->left, probe ) )
			return false;

		if ( !item->count ) {

			out = storage::integer_value ( item->negated ? 1 : 0 );

			return true;

		}

		if ( probe.kind == storage::VALUE_NULL ) {

			out = storage::null_value ( );

			return true;

		}

		truth_kind result = TRUTH_FALSE;

		for ( uint32_t i = 0; i < item->count && result != TRUTH_TRUE; i++ ) {

			storage::value candidate;

			if ( !evaluate ( ctx, scope, item->list [ i ], candidate ) )
				return false;

			truth_kind here = compare_expressions ( ctx, sql::OPERATOR_EQUAL, item->left, probe, item->list [ i ], candidate );

			if ( here == TRUTH_TRUE )
				result = TRUTH_TRUE;
			else if ( here == TRUTH_NULL )
				result = TRUTH_NULL;

		}

		out = truth_value ( item->negated ? negate ( result ) : result );

		return true;

	}

	static bool evaluate_like ( run_context& ctx, query_state* scope, const sql::expression* item, storage::value& out ) {

		storage::value text;
		storage::value pattern;
		storage::value escape = storage::null_value ( );

		if ( !evaluate ( ctx, scope, item->left, text ) || !evaluate ( ctx, scope, item->right, pattern ) || ( item->third && !evaluate ( ctx, scope, item->third, escape ) ) )
			return false;

		if ( text.kind == storage::VALUE_NULL || pattern.kind == storage::VALUE_NULL || ( item->third && escape.kind == storage::VALUE_NULL ) ) {

			out = storage::null_value ( );

			return true;

		}

		uint32_t escape_code = 0;

		if ( item->third ) {

			storage::value written = text_of ( *ctx.scratch, escape );
			size_t at = 0;

			if ( !written.size )
				return fail_run ( ctx, "ESCAPE needs exactly one character" );

			uint8_t first = written.bytes [ 0 ];
			uint32_t length = first >= 0xF0 ? 4 : first >= 0xE0 ? 3 : first >= 0xC0 ? 2 : 1;

			if ( length != written.size )
				return fail_run ( ctx, "ESCAPE needs exactly one character" );

			escape_code = first & ( length == 1 ? 0x7F : 0x3F >> ( length - 1 ) );

			for ( at = 1; at < length; at++ )
				escape_code = escape_code << 6 | ( written.bytes [ at ] & 0x3F );

		}

		storage::value a = text_of ( *ctx.scratch, text );
		storage::value b = text_of ( *ctx.scratch, pattern );
		bool hit = like_match ( b.bytes, b.size, a.bytes, a.size, escape_code, item->star, ctx.schema->postgres && !item->nocase );

		out = storage::integer_value ( hit != item->negated ? 1 : 0 );

		return true;

	}

	static bool evaluate_case ( run_context& ctx, query_state* scope, const sql::expression* item, storage::value& out ) {

		storage::value base_value = storage::null_value ( );

		if ( item->left && !evaluate ( ctx, scope, item->left, base_value ) )
			return false;

		for ( uint32_t i = 0; i + 1 < item->count; i += 2 ) {

			bool chosen = false;

			if ( item->left ) {

				storage::value candidate;

				if ( !evaluate ( ctx, scope, item->list [ i ], candidate ) )
					return false;

				chosen = compare_expressions ( ctx, sql::OPERATOR_EQUAL, item->left, base_value, item->list [ i ], candidate ) == TRUTH_TRUE;

			} else {

				truth_kind truth = TRUTH_NULL;

				if ( !condition ( ctx, scope, item->list [ i ], truth ) )
					return false;

				chosen = truth == TRUTH_TRUE;

			}

			if ( chosen )
				return evaluate ( ctx, scope, item->list [ i + 1 ], out );

		}

		if ( item->right )
			return evaluate ( ctx, scope, item->right, out );

		out = storage::null_value ( );

		return true;

	}

	static bool evaluate_binary ( run_context& ctx, query_state* scope, const sql::expression* item, storage::value& out ) {

		if ( item->op == sql::OPERATOR_AND || item->op == sql::OPERATOR_OR ) {

			truth_kind left = TRUTH_NULL;

			if ( !condition ( ctx, scope, item->left, left ) )
				return false;

			if ( item->op == sql::OPERATOR_AND && left == TRUTH_FALSE ) {

				out = storage::integer_value ( 0 );

				return true;

			}

			if ( item->op == sql::OPERATOR_OR && left == TRUTH_TRUE ) {

				out = storage::integer_value ( 1 );

				return true;

			}

			truth_kind right = TRUTH_NULL;

			if ( !condition ( ctx, scope, item->right, right ) )
				return false;

			if ( item->op == sql::OPERATOR_AND )
				out = truth_value ( both ( left, right ) );
			else
				out = truth_value ( right == TRUTH_TRUE ? TRUTH_TRUE : left == TRUTH_NULL || right == TRUTH_NULL ? TRUTH_NULL : TRUTH_FALSE );

			return true;

		}

		storage::value a;
		storage::value b;

		if ( !evaluate ( ctx, scope, item->left, a ) || !evaluate ( ctx, scope, item->right, b ) )
			return false;

		switch ( item->op ) {

			case sql::OPERATOR_EQUAL:
			case sql::OPERATOR_NOT_EQUAL:
			case sql::OPERATOR_LESS:
			case sql::OPERATOR_LESS_EQUAL:
			case sql::OPERATOR_GREATER:
			case sql::OPERATOR_GREATER_EQUAL:
			case sql::OPERATOR_IS:
			case sql::OPERATOR_IS_NOT:
				out = truth_value ( compare_expressions ( ctx, item->op, item->left, a, item->right, b ) );

				return true;

			case sql::OPERATOR_CONCAT:
				return concatenate ( ctx, a, b, out );

			case sql::OPERATOR_JSON:
			case sql::OPERATOR_JSON_VALUE:
				return json_arrow ( ctx, a, b, item->op == sql::OPERATOR_JSON_VALUE, out );

			default:
				if ( ( a.kind == storage::VALUE_TEXT || b.kind == storage::VALUE_TEXT ) && ctx.schema->postgres ) {

					bool handled = false;

					if ( !moment_arithmetic ( ctx, item->op, a, b, out, handled ) )
						return false;

					if ( handled )
						return true;

				}

				if ( ctx.schema->postgres && ( item->op == sql::OPERATOR_DIVIDE || item->op == sql::OPERATOR_MODULO ) && a.kind != storage::VALUE_NULL && b.kind != storage::VALUE_NULL ) {

					storage::value divisor = numeric_of ( b );

					if ( ( divisor.kind == storage::VALUE_INTEGER && !divisor.integer ) || ( divisor.kind == storage::VALUE_REAL && divisor.real == 0 ) || ( divisor.kind == storage::VALUE_DECIMAL && !storage::decimal_mantissa ( divisor ) ) )
						return fail_run ( ctx, "division by zero" );

				}

				out = arithmetic ( item->op, a, b );

				return true;

		}

	}

	static bool evaluate_unary ( run_context& ctx, query_state* scope, const sql::expression* item, storage::value& out ) {

		storage::value inner;

		if ( !evaluate ( ctx, scope, item->left, inner ) )
			return false;

		switch ( item->op ) {

			case sql::OPERATOR_NOT:
				out = truth_value ( negate ( truth_of ( inner ) ) );

				return true;

			case sql::OPERATOR_NEGATE: {

				if ( inner.kind == storage::VALUE_NULL ) {

					out = inner;

					return true;

				}

				storage::value number = numeric_of ( inner );

				if ( number.kind == storage::VALUE_INTEGER )
					out = number.integer == static_cast < int64_t > ( 0x8000000000000000ull ) ? storage::real_value ( 9223372036854775808.0 ) : storage::integer_value ( -number.integer );
				else if ( number.kind == storage::VALUE_DECIMAL )
					out = storage::decimal_raw ( -storage::decimal_mantissa ( number ), number.subtype );
				else if ( number.real > 9223372036854774784.0 && number.real < 9223372036854777856.0 && item->left && item->left->kind == sql::EXPRESSION_LITERAL )
					out = storage::integer_value ( static_cast < int64_t > ( 0x8000000000000000ull ) );
				else
					out = storage::real_value ( -number.real );

				return true;

			}

			case sql::OPERATOR_BIT_NOT: {

				int64_t number = 0;

				if ( !integer_of ( inner, number ) ) {

					out = storage::null_value ( );

					return true;

				}

				out = storage::integer_value ( ~number );

				return true;

			}

			default:
				out = inner;

				return true;

		}

	}

	bool evaluate ( run_context& ctx, query_state* scope, const sql::expression* item, storage::value& out ) {

		if ( !item ) {

			out = storage::null_value ( );

			return true;

		}

		switch ( item->kind ) {

			case sql::EXPRESSION_LITERAL:
				out = item->literal;

				if ( out.kind == storage::VALUE_DECIMAL && !ctx.schema->postgres )
					out = storage::real_value ( item->literal.real );

				return true;

			case sql::EXPRESSION_PARAMETER:
				out = item->parameter < ctx.parameter_count ? ctx.parameters [ item->parameter ] : storage::null_value ( );

				return true;

			case sql::EXPRESSION_COLUMN: {

				query_state* owner = scope;

				for ( uint32_t i = 0; i < item->depth && owner; i++ )
					owner = owner->parent;

				if ( !owner || item->source < 0 )
					return fail_named ( ctx, "the column ", item->name, " cannot be read here" );

				source_state& source = owner->sources [ item->source ];

				if ( source.null_row || !source.values ) {

					const prepared_source* definition = source.plan;
					int32_t paired = item->column >= 0 && definition && definition->paired_source ? definition->paired_source [ item->column ] : -1;
					source_state* other = paired >= 0 ? &owner->sources [ paired ] : nullptr;

					if ( other && !other->null_row && other->values )
						out = other->values [ definition->paired_column [ item->column ] ];
					else
						out = storage::null_value ( );

				} else if ( item->column < 0 )
					out = storage::integer_value ( source.rowid );
				else
					out = source.values [ item->column ];

				return true;

			}

			case sql::EXPRESSION_UNARY:
				return evaluate_unary ( ctx, scope, item, out );

			case sql::EXPRESSION_BINARY:
				return evaluate_binary ( ctx, scope, item, out );

			case sql::EXPRESSION_FUNCTION: {

				if ( item->window ) {

					if ( !scope || !scope->window_values || item->window_index < 0 )
						return fail_named ( ctx, "the window function ", item->name, "() cannot be used here" );

					out = scope->window_values [ item->window_index ];

					return true;

				}

				if ( item->aggregate >= 0 ) {

					if ( !scope || !scope->aggregate_values )
						return fail_named ( ctx, "the aggregate ", item->name, "() cannot be used here" );

					out = scope->aggregate_values [ item->aggregate ];

					return true;

				}

				if ( search_function ( item->column ) )
					return call_search_function ( ctx, scope, item, out );

				storage::value* args = item->count ? ctx.scratch->make < storage::value > ( item->count ) : nullptr;

				if ( item->count && !args )
					return fail_run ( ctx, "out of memory" );

				for ( uint32_t i = 0; i < item->count; i++ )
					if ( !evaluate ( ctx, scope, item->list [ i ], args [ i ] ) )
						return false;

				return call_function ( ctx, item, args, item->count, out );

			}

			case sql::EXPRESSION_IN:
				if ( item->query )
					return run_child ( ctx, scope, item, out );

				return evaluate_in_list ( ctx, scope, item, out );

			case sql::EXPRESSION_BETWEEN: {

				storage::value value;
				storage::value low;
				storage::value high;

				if ( !evaluate ( ctx, scope, item->left, value ) || !evaluate ( ctx, scope, item->right, low ) || !evaluate ( ctx, scope, item->third, high ) )
					return false;

				truth_kind result = both ( compare_expressions ( ctx, sql::OPERATOR_GREATER_EQUAL, item->left, value, item->right, low ), compare_expressions ( ctx, sql::OPERATOR_LESS_EQUAL, item->left, value, item->third, high ) );

				out = truth_value ( item->negated ? negate ( result ) : result );

				return true;

			}

			case sql::EXPRESSION_NULL_TEST: {

				storage::value value;

				if ( !evaluate ( ctx, scope, item->left, value ) )
					return false;

				out = storage::integer_value ( ( value.kind == storage::VALUE_NULL ) != item->negated ? 1 : 0 );

				return true;

			}

			case sql::EXPRESSION_LIKE:
				return evaluate_like ( ctx, scope, item, out );

			case sql::EXPRESSION_MATCH:
				return evaluate_match ( ctx, scope, item, out );

			case sql::EXPRESSION_CASE:
				return evaluate_case ( ctx, scope, item, out );

			case sql::EXPRESSION_CAST: {

				storage::value value;

				if ( !evaluate ( ctx, scope, item->left, value ) )
					return false;

				if ( value.kind == storage::VALUE_TEXT && item->name && ( base::same_folded ( item->name, "bool" ) || base::same_folded ( item->name, "boolean" ) ) ) {

					int32_t truth = text_truth ( reinterpret_cast < const char* > ( value.bytes ), value.size );

					if ( truth < 0 )
						return fail_run ( ctx, "invalid input syntax for type boolean" );

					out = storage::integer_value ( truth );

					return true;

				}

				if ( ctx.schema->postgres && item->name && base::find_folded ( item->name, "interval" ) ) {

					interval_parts parts = { };

					if ( value.kind == storage::VALUE_NULL ) {

						out = value;

						return true;

					}

					storage::value written = text_of ( *ctx.scratch, value );

					if ( !parse_interval ( written.bytes, written.size, parts ) ) {

						base::text message;

						message.add ( "invalid input syntax for type interval: \"" ).add ( reinterpret_cast < const char* > ( written.bytes ), base::min < size_t > ( written.size, 64 ) ).add ( '"' );

						return fail_run ( ctx, message.value ( ) );

					}

					return interval_text ( ctx, parts, out );

				}

				int32_t precision = -1;
				int32_t scale = -1;
				moment_kind moment = ctx.schema->postgres ? moment_type ( item->name ) : MOMENT_NONE;

				if ( moment && value.kind == storage::VALUE_TEXT ) {

					base::text written;

					if ( !canonical_moment ( value.bytes, value.size, moment, written ) ) {

						base::text message;

						message.add ( "invalid input syntax for type " ).add ( item->name ).add ( ": \"" ).add ( reinterpret_cast < const char* > ( value.bytes ), base::min < size_t > ( value.size, 64 ) ).add ( '"' );

						return fail_run ( ctx, message.value ( ) );

					}

					char* kept = ctx.scratch->keep ( written.value ( ), written.size );

					if ( !kept )
						return fail_run ( ctx, "out of memory" );

					out = storage::text_value ( kept, written.size );

					return true;

				}

				if ( ctx.schema->postgres && value.kind != storage::VALUE_NULL && value.kind != storage::VALUE_BLOB && decimal_type ( item->name, precision, scale ) ) {

					decimal_number exact = { };

					if ( !decimal_of ( value, exact ) ) {

						if ( value.kind == storage::VALUE_TEXT )
							return cast_value ( ctx, value, item->cast, out );

						return fail_run ( ctx, "numeric field overflow" );

					}

					if ( scale >= 0 && !decimal_rescale ( exact, static_cast < uint32_t > ( scale ) ) )
						return fail_run ( ctx, "numeric field overflow" );

					if ( precision > 0 && static_cast < int32_t > ( decimal_whole_digits ( exact ) ) > precision - base::max ( scale, 0 ) )
						return fail_run ( ctx, "numeric field overflow" );

					out = decimal_result ( exact );

					return true;

				}

				return cast_value ( ctx, value, item->cast, out );

			}

			case sql::EXPRESSION_SUBQUERY:
			case sql::EXPRESSION_EXISTS:
				return run_child ( ctx, scope, item, out );

		}

		return fail_run ( ctx, "this expression cannot be evaluated" );

	}

}
