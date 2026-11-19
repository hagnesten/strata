#include "engine/value/value.hh"
#include "base/hash/crc32c.hh"

namespace engine {

	bool read_number_text ( const uint8_t* bytes, size_t size, storage::value& out, bool whole ) {

		size_t at = 0;

		while ( at < size && ( bytes [ at ] == ' ' || bytes [ at ] == '\t' || bytes [ at ] == '\n' || bytes [ at ] == '\r' ) )
			at++;

		bool negative = false;

		if ( at < size && ( bytes [ at ] == '+' || bytes [ at ] == '-' ) )
			negative = bytes [ at++ ] == '-';

		size_t digits_start = at;
		uint64_t integer = 0;
		bool overflow = false;
		double real = 0;

		while ( at < size && bytes [ at ] >= '0' && bytes [ at ] <= '9' ) {

			uint64_t digit = static_cast < uint64_t > ( bytes [ at ] - '0' );

			if ( integer > ( 0x7FFFFFFFFFFFFFFFull - digit ) / 10 )
				overflow = true;
			else
				integer = integer * 10 + digit;

			real = real * 10 + static_cast < double > ( digit );
			at++;

		}

		bool fraction = false;
		bool any = at > digits_start;

		if ( at < size && bytes [ at ] == '.' ) {

			double scale = 0.1;

			fraction = true;
			at++;

			while ( at < size && bytes [ at ] >= '0' && bytes [ at ] <= '9' ) {

				real += static_cast < double > ( bytes [ at ] - '0' ) * scale;
				scale /= 10;
				any = true;
				at++;

			}

		}

		if ( !any ) {

			out = storage::integer_value ( 0 );

			return false;

		}

		if ( at < size && ( bytes [ at ] == 'e' || bytes [ at ] == 'E' ) ) {

			size_t back = at;
			bool down = false;
			int32_t exponent = 0;

			at++;

			if ( at < size && ( bytes [ at ] == '+' || bytes [ at ] == '-' ) )
				down = bytes [ at++ ] == '-';

			if ( at < size && bytes [ at ] >= '0' && bytes [ at ] <= '9' ) {

				while ( at < size && bytes [ at ] >= '0' && bytes [ at ] <= '9' )
					exponent = base::min ( exponent * 10 + ( bytes [ at++ ] - '0' ), 400 );

				double power = 1;

				for ( int32_t i = 0; i < exponent; i++ )
					power *= 10;

				real = down ? real / power : real * power;
				fraction = true;

			} else
				at = back;

		}

		size_t end = at;

		while ( end < size && ( bytes [ end ] == ' ' || bytes [ end ] == '\t' || bytes [ end ] == '\n' || bytes [ end ] == '\r' ) )
			end++;

		if ( fraction || overflow )
			out = storage::real_value ( negative ? -real : real );
		else
			out = storage::integer_value ( negative ? -static_cast < int64_t > ( integer ) : static_cast < int64_t > ( integer ) );

		return !whole || end == size;

	}

	storage::value numeric_of ( const storage::value& item ) {

		if ( item.kind == storage::VALUE_INTEGER || item.kind == storage::VALUE_REAL || item.kind == storage::VALUE_NULL )
			return item;

		storage::value out;

		read_number_text ( item.bytes, item.size, out, false );

		return out;

	}

	bool real_of ( const storage::value& item, double& out ) {

		storage::value number = numeric_of ( item );

		if ( number.kind == storage::VALUE_NULL )
			return false;

		out = number.kind == storage::VALUE_INTEGER ? static_cast < double > ( number.integer ) : number.real;

		return true;

	}

	bool integer_of ( const storage::value& item, int64_t& out ) {

		storage::value number = numeric_of ( item );

		if ( number.kind == storage::VALUE_NULL )
			return false;

		if ( number.kind == storage::VALUE_INTEGER )
			out = number.integer;
		else if ( number.real >= 9.2e18 )
			out = 0x7FFFFFFFFFFFFFFFll;
		else if ( number.real <= -9.2e18 )
			out = static_cast < int64_t > ( 0x8000000000000000ull );
		else
			out = static_cast < int64_t > ( number.real );

		return true;

	}

	truth_kind truth_of ( const storage::value& item ) {

		if ( item.kind == storage::VALUE_NULL )
			return TRUTH_NULL;

		storage::value number = numeric_of ( item );

		if ( number.kind == storage::VALUE_INTEGER )
			return number.integer ? TRUTH_TRUE : TRUTH_FALSE;

		return number.real != 0 ? TRUTH_TRUE : TRUTH_FALSE;

	}

	storage::value truth_value ( truth_kind truth ) {

		return truth == TRUTH_NULL ? storage::null_value ( ) : storage::integer_value ( truth == TRUTH_TRUE ? 1 : 0 );

	}

	storage::value keep_value ( base::arena& memory, const storage::value& item ) {

		if ( ( item.kind != storage::VALUE_TEXT && item.kind != storage::VALUE_BLOB ) || !item.size )
			return item;

		storage::value out = item;
		char* kept = memory.keep ( reinterpret_cast < const char* > ( item.bytes ), item.size );

		out.bytes = reinterpret_cast < const uint8_t* > ( kept );

		return kept ? out : storage::null_value ( );

	}

	storage::value text_of ( base::arena& memory, const storage::value& item ) {

		if ( item.kind == storage::VALUE_TEXT || item.kind == storage::VALUE_NULL )
			return item;

		if ( item.kind == storage::VALUE_BLOB )
			return storage::text_value ( reinterpret_cast < const char* > ( item.bytes ), item.size );

		base::text made;

		if ( item.kind == storage::VALUE_INTEGER )
			made.signed_number ( item.integer );
		else
			made.real ( item.real );

		char* kept = memory.keep ( made.value ( ), made.size );

		return kept ? storage::text_value ( kept, made.size ) : storage::null_value ( );

	}

	storage::value apply_affinity ( base::arena& memory, const storage::value& item, sql::affinity_kind affinity ) {

		switch ( affinity ) {

			case sql::AFFINITY_INTEGER:
			case sql::AFFINITY_NUMERIC:
			case sql::AFFINITY_REAL: {

				storage::value number = item;

				if ( item.kind == storage::VALUE_TEXT ) {

					storage::value parsed;

					if ( !read_number_text ( item.bytes, item.size, parsed, true ) )
						return item;

					number = parsed;

				}

				if ( affinity == sql::AFFINITY_REAL && number.kind == storage::VALUE_INTEGER )
					return storage::real_value ( static_cast < double > ( number.integer ) );

				if ( affinity != sql::AFFINITY_REAL && number.kind == storage::VALUE_REAL && number.real == static_cast < double > ( static_cast < int64_t > ( number.real ) ) && number.real > -9.2e18 && number.real < 9.2e18 )
					return storage::integer_value ( static_cast < int64_t > ( number.real ) );

				return number;

			}

			case sql::AFFINITY_TEXT:
				if ( item.kind == storage::VALUE_INTEGER || item.kind == storage::VALUE_REAL )
					return text_of ( memory, item );

				return item;

			default:
				return item;

		}

	}

	static storage::value real_result ( double number ) {

		return storage::real_value ( number );

	}

	storage::value arithmetic ( sql::operator_kind op, const storage::value& a, const storage::value& b ) {

		if ( a.kind == storage::VALUE_NULL || b.kind == storage::VALUE_NULL )
			return storage::null_value ( );

		storage::value x = numeric_of ( a );
		storage::value y = numeric_of ( b );

		if ( op == sql::OPERATOR_BIT_AND || op == sql::OPERATOR_BIT_OR || op == sql::OPERATOR_SHIFT_LEFT || op == sql::OPERATOR_SHIFT_RIGHT ) {

			int64_t left = 0;
			int64_t right = 0;

			integer_of ( x, left );
			integer_of ( y, right );

			if ( op == sql::OPERATOR_BIT_AND )
				return storage::integer_value ( left & right );

			if ( op == sql::OPERATOR_BIT_OR )
				return storage::integer_value ( left | right );

			if ( op == sql::OPERATOR_SHIFT_RIGHT ) {

				right = -right;

			}

			if ( right >= 64 || right <= -64 )
				return storage::integer_value ( right > 0 || left >= 0 ? 0 : -1 );

			if ( right >= 0 )
				return storage::integer_value ( static_cast < int64_t > ( static_cast < uint64_t > ( left ) << right ) );

			return storage::integer_value ( left >> -right );

		}

		if ( x.kind == storage::VALUE_INTEGER && y.kind == storage::VALUE_INTEGER ) {

			int64_t left = x.integer;
			int64_t right = y.integer;
			int64_t result = 0;

			switch ( op ) {

				case sql::OPERATOR_ADD:
					if ( !__builtin_add_overflow ( left, right, &result ) )
						return storage::integer_value ( result );

					return real_result ( static_cast < double > ( left ) + static_cast < double > ( right ) );

				case sql::OPERATOR_SUBTRACT:
					if ( !__builtin_sub_overflow ( left, right, &result ) )
						return storage::integer_value ( result );

					return real_result ( static_cast < double > ( left ) - static_cast < double > ( right ) );

				case sql::OPERATOR_MULTIPLY:
					if ( !__builtin_mul_overflow ( left, right, &result ) )
						return storage::integer_value ( result );

					return real_result ( static_cast < double > ( left ) * static_cast < double > ( right ) );

				case sql::OPERATOR_DIVIDE:
					if ( !right )
						return storage::null_value ( );

					if ( right == -1 && left == static_cast < int64_t > ( 0x8000000000000000ull ) )
						return real_result ( 9223372036854775808.0 );

					return storage::integer_value ( left / right );

				case sql::OPERATOR_MODULO:
					if ( !right )
						return storage::null_value ( );

					if ( right == -1 )
						return storage::integer_value ( 0 );

					return storage::integer_value ( left % right );

				default:
					return storage::null_value ( );

			}

		}

		double left = x.kind == storage::VALUE_INTEGER ? static_cast < double > ( x.integer ) : x.real;
		double right = y.kind == storage::VALUE_INTEGER ? static_cast < double > ( y.integer ) : y.real;

		switch ( op ) {

			case sql::OPERATOR_ADD:
				return real_result ( left + right );

			case sql::OPERATOR_SUBTRACT:
				return real_result ( left - right );

			case sql::OPERATOR_MULTIPLY:
				return real_result ( left * right );

			case sql::OPERATOR_DIVIDE:
				return right == 0 ? storage::null_value ( ) : real_result ( left / right );

			case sql::OPERATOR_MODULO: {

				int64_t whole_left = static_cast < int64_t > ( left );
				int64_t whole_right = static_cast < int64_t > ( right );

				if ( !whole_right )
					return storage::null_value ( );

				if ( whole_right == -1 )
					return real_result ( 0 );

				return real_result ( static_cast < double > ( whole_left % whole_right ) );

			}

			default:
				return storage::null_value ( );

		}

	}

	int32_t order_values ( const storage::value& a, const storage::value& b ) {

		return storage::compare_values ( a, b );

	}

	truth_kind compare_with ( sql::operator_kind op, const storage::value& a, const storage::value& b ) {

		if ( op == sql::OPERATOR_IS || op == sql::OPERATOR_IS_NOT ) {

			bool equal = ( a.kind == storage::VALUE_NULL && b.kind == storage::VALUE_NULL ) || ( a.kind != storage::VALUE_NULL && b.kind != storage::VALUE_NULL && order_values ( a, b ) == 0 );

			return ( op == sql::OPERATOR_IS ) == equal ? TRUTH_TRUE : TRUTH_FALSE;

		}

		if ( a.kind == storage::VALUE_NULL || b.kind == storage::VALUE_NULL )
			return TRUTH_NULL;

		int32_t order = order_values ( a, b );
		bool result = false;

		switch ( op ) {

			case sql::OPERATOR_EQUAL: result = order == 0; break;
			case sql::OPERATOR_NOT_EQUAL: result = order != 0; break;
			case sql::OPERATOR_LESS: result = order < 0; break;
			case sql::OPERATOR_LESS_EQUAL: result = order <= 0; break;
			case sql::OPERATOR_GREATER: result = order > 0; break;
			case sql::OPERATOR_GREATER_EQUAL: result = order >= 0; break;
			default: return TRUTH_NULL;

		}

		return result ? TRUTH_TRUE : TRUTH_FALSE;

	}

	static uint32_t next_code ( const uint8_t* text, size_t size, size_t& at ) {

		uint8_t first = text [ at++ ];

		if ( first < 0x80 )
			return first;

		uint32_t length = first >= 0xF0 ? 3 : first >= 0xE0 ? 2 : first >= 0xC0 ? 1 : 0;
		uint32_t code = first & ( 0x3F >> length );

		for ( uint32_t i = 0; i < length && at < size && ( text [ at ] & 0xC0 ) == 0x80; i++ )
			code = code << 6 | ( text [ at++ ] & 0x3F );

		return code;

	}

	static uint32_t fold ( uint32_t code ) {

		return code >= 'A' && code <= 'Z' ? code + 32 : code;

	}

	bool like_match ( const uint8_t* pattern, size_t pattern_size, const uint8_t* text, size_t text_size, uint32_t escape, bool glob ) {

		size_t p = 0;
		size_t t = 0;
		size_t star_p = static_cast < size_t > ( -1 );
		size_t star_t = 0;

		for ( uint32_t guard = 0; guard < 100000000; guard++ ) {

			if ( p < pattern_size ) {

				size_t next_p = p;
				uint32_t wanted = next_code ( pattern, pattern_size, next_p );
				bool literal = false;

				if ( !glob && escape && wanted == escape && next_p < pattern_size ) {

					wanted = next_code ( pattern, pattern_size, next_p );
					literal = true;

				}

				uint32_t many = glob ? '*' : '%';
				uint32_t one = glob ? '?' : '_';

				if ( !literal && wanted == many ) {

					while ( next_p < pattern_size ) {

						size_t peek = next_p;

						if ( next_code ( pattern, pattern_size, peek ) != many )
							break;

						next_p = peek;

					}

					if ( next_p >= pattern_size )
						return true;

					star_p = next_p;
					star_t = t;
					p = next_p;

					continue;

				}

				if ( t < text_size ) {

					size_t next_t = t;
					uint32_t have = next_code ( text, text_size, next_t );
					bool hit = false;

					if ( !literal && wanted == one )
						hit = true;
					else if ( glob && !literal && wanted == '[' ) {

						size_t at = next_p;
						bool invert = false;
						bool matched = false;

						if ( at < pattern_size && pattern [ at ] == '^' ) {

							invert = true;
							at++;

						}

						bool first = true;

						while ( at < pattern_size && ( first || pattern [ at ] != ']' ) ) {

							uint32_t low = next_code ( pattern, pattern_size, at );
							uint32_t high = low;

							first = false;

							if ( at + 1 < pattern_size && pattern [ at ] == '-' && pattern [ at + 1 ] != ']' ) {

								at++;
								high = next_code ( pattern, pattern_size, at );

							}

							if ( have >= low && have <= high )
								matched = true;

						}

						if ( at < pattern_size )
							at++;

						hit = matched != invert;
						next_p = at;

					} else
						hit = glob ? wanted == have : fold ( wanted ) == fold ( have );

					if ( hit ) {

						p = next_p;
						t = next_t;

						continue;

					}

				}

			} else if ( t == text_size )
				return true;

			if ( star_p == static_cast < size_t > ( -1 ) || star_t >= text_size )
				return false;

			next_code ( text, text_size, star_t );
			t = star_t;
			p = star_p;

		}

		return false;

	}

	uint32_t hash_value ( const storage::value& item, uint32_t seed ) {

		uint8_t kind = item.kind == storage::VALUE_REAL ? storage::VALUE_INTEGER : item.kind;

		seed = base::crc32c ( &kind, 1, seed );

		if ( item.kind == storage::VALUE_INTEGER || item.kind == storage::VALUE_REAL ) {

			double real = item.kind == storage::VALUE_INTEGER ? static_cast < double > ( item.integer ) : item.real;

			if ( item.kind == storage::VALUE_REAL && real == static_cast < double > ( static_cast < int64_t > ( real ) ) && real > -9.2e18 && real < 9.2e18 ) {

				int64_t whole = static_cast < int64_t > ( real );

				return base::crc32c ( &whole, 8, seed );

			}

			if ( item.kind == storage::VALUE_INTEGER )
				return base::crc32c ( &item.integer, 8, seed );

			return base::crc32c ( &real, 8, seed );

		}

		if ( item.kind == storage::VALUE_TEXT || item.kind == storage::VALUE_BLOB )
			return base::crc32c ( item.bytes, item.size, seed );

		return seed;

	}

	bool same_value ( const storage::value& a, const storage::value& b ) {

		if ( a.kind == storage::VALUE_NULL || b.kind == storage::VALUE_NULL )
			return a.kind == b.kind;

		return order_values ( a, b ) == 0;

	}

}
