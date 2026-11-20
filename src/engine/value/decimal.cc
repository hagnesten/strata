#include "engine/value/decimal.hh"

namespace engine {

	typedef unsigned __int128 decimal_unsigned;

	static __int128 power_of_ten ( uint32_t exponent ) {

		__int128 value = 1;

		for ( uint32_t i = 0; i < exponent; i++ )
			value *= 10;

		return value;

	}

	static decimal_unsigned magnitude ( __int128 value ) {

		return value < 0 ? static_cast < decimal_unsigned > ( -( value + 1 ) ) + 1 : static_cast < decimal_unsigned > ( value );

	}

	static uint32_t digit_count ( decimal_unsigned value ) {

		uint32_t count = 1;

		while ( value >= 10 ) {

			value /= 10;
			count++;

		}

		return count;

	}

	static bool fits ( __int128 value ) {

		static const decimal_unsigned limit = static_cast < decimal_unsigned > ( power_of_ten ( DECIMAL_MAX_DIGITS ) );

		return magnitude ( value ) < limit;

	}

	bool decimal_of ( const storage::value& item, decimal_number& out ) {

		if ( item.kind == storage::VALUE_DECIMAL ) {

			out.mantissa = storage::decimal_mantissa ( item );
			out.scale = item.subtype;

			return true;

		}

		if ( item.kind == storage::VALUE_INTEGER ) {

			out.mantissa = item.integer;
			out.scale = 0;

			return true;

		}

		if ( item.kind == storage::VALUE_REAL )
			return decimal_from_real ( item.real, out );

		if ( item.kind == storage::VALUE_TEXT )
			return parse_decimal ( item.bytes, item.size, out );

		return false;

	}

	storage::value decimal_result ( const decimal_number& item ) {

		return storage::decimal_raw ( item.mantissa, static_cast < uint8_t > ( item.scale ) );

	}

	bool parse_decimal ( const uint8_t* text, size_t size, decimal_number& out ) {

		size_t at = 0;

		while ( at < size && ( text [ at ] == ' ' || text [ at ] == '\t' || text [ at ] == '\r' || text [ at ] == '\n' ) )
			at++;

		bool negative = false;

		if ( at < size && ( text [ at ] == '+' || text [ at ] == '-' ) )
			negative = text [ at++ ] == '-';

		__int128 mantissa = 0;
		uint32_t scale = 0;
		uint32_t digits = 0;
		bool any = false;
		bool fraction = false;

		for ( ; at < size; at++ ) {

			uint8_t c = text [ at ];

			if ( c == '.' && !fraction ) {

				fraction = true;

				continue;

			}

			if ( c < '0' || c > '9' )
				break;

			any = true;

			if ( !digits && c == '0' && !fraction )
				continue;

			if ( digits >= DECIMAL_MAX_DIGITS ) {

				if ( fraction )
					continue;

				return false;

			}

			mantissa = mantissa * 10 + ( c - '0' );
			digits++;

			if ( fraction )
				scale++;

		}

		if ( !any )
			return false;

		if ( at < size && ( text [ at ] == 'e' || text [ at ] == 'E' ) ) {

			at++;

			bool down = false;
			int32_t exponent = 0;

			if ( at < size && ( text [ at ] == '+' || text [ at ] == '-' ) )
				down = text [ at++ ] == '-';

			if ( at >= size || text [ at ] < '0' || text [ at ] > '9' )
				return false;

			while ( at < size && text [ at ] >= '0' && text [ at ] <= '9' )
				exponent = base::min ( exponent * 10 + ( text [ at++ ] - '0' ), 1000 );

			if ( down )
				scale += static_cast < uint32_t > ( exponent );
			else {

				while ( exponent > 0 && scale > 0 ) {

					scale--;
					exponent--;

				}

				while ( exponent-- > 0 ) {

					mantissa *= 10;

					if ( !fits ( mantissa ) )
						return false;

				}

			}

		}

		while ( at < size && ( text [ at ] == ' ' || text [ at ] == '\t' || text [ at ] == '\r' || text [ at ] == '\n' ) )
			at++;

		if ( at != size )
			return false;

		out.mantissa = negative ? -mantissa : mantissa;
		out.scale = scale;

		if ( out.scale > DECIMAL_MAX_SCALE && !decimal_rescale ( out, DECIMAL_MAX_SCALE ) )
			return false;

		return true;

	}

	bool decimal_from_real ( double number, decimal_number& out ) {

		if ( number != number || number > 1e35 || number < -1e35 )
			return false;

		base::text written;

		written.real ( number );

		return parse_decimal ( reinterpret_cast < const uint8_t* > ( written.value ( ) ), written.size, out );

	}

	double decimal_real ( const decimal_number& item ) {

		double value = static_cast < double > ( item.mantissa );
		double divisor = 1;

		for ( uint32_t i = 0; i < item.scale; i++ )
			divisor *= 10;

		return value / divisor;

	}

	void decimal_text ( const decimal_number& item, base::text& out ) {

		decimal_unsigned value = magnitude ( item.mantissa );
		char digits [ 48 ];
		uint32_t count = 0;

		do {

			digits [ count++ ] = static_cast < char > ( '0' + static_cast < uint32_t > ( value % 10 ) );
			value /= 10;

		} while ( value );

		while ( count <= item.scale )
			digits [ count++ ] = '0';

		if ( item.mantissa < 0 )
			out.add ( '-' );

		for ( uint32_t i = count; i > 0; i-- ) {

			if ( i == item.scale && item.scale )
				out.add ( '.' );

			out.add ( digits [ i - 1 ] );

		}

	}

	bool decimal_rescale ( decimal_number& item, uint32_t scale ) {

		if ( scale == item.scale )
			return true;

		if ( scale > item.scale ) {

			__int128 grown = item.mantissa;

			for ( uint32_t i = item.scale; i < scale; i++ ) {

				grown *= 10;

				if ( !fits ( grown ) )
					return false;

			}

			item.mantissa = grown;
			item.scale = scale;

			return true;

		}

		__int128 divisor = power_of_ten ( item.scale - scale );
		__int128 whole = item.mantissa / divisor;
		__int128 rest = item.mantissa % divisor;
		decimal_unsigned twice = magnitude ( rest ) * 2;

		if ( twice >= static_cast < decimal_unsigned > ( divisor ) )
			whole += item.mantissa < 0 ? -1 : 1;

		item.mantissa = whole;
		item.scale = scale;

		return true;

	}

	uint32_t decimal_whole_digits ( const decimal_number& item ) {

		decimal_unsigned whole = magnitude ( item.mantissa ) / static_cast < decimal_unsigned > ( power_of_ten ( item.scale ) );

		return whole ? digit_count ( whole ) : 0;

	}

	void decimal_trim ( decimal_number& item ) {

		while ( item.scale && item.mantissa % 10 == 0 ) {

			item.mantissa /= 10;
			item.scale--;

		}

	}

	bool decimal_integral ( const decimal_number& item, int64_t& out ) {

		__int128 divisor = power_of_ten ( item.scale );

		if ( item.mantissa % divisor )
			return false;

		__int128 whole = item.mantissa / divisor;

		if ( whole > static_cast < __int128 > ( 0x7FFFFFFFFFFFFFFFll ) || whole < -static_cast < __int128 > ( 0x7FFFFFFFFFFFFFFFll ) - 1 )
			return false;

		out = static_cast < int64_t > ( whole );

		return true;

	}

	int32_t decimal_compare ( const decimal_number& a, const decimal_number& b ) {

		__int128 a_divisor = power_of_ten ( a.scale );
		__int128 b_divisor = power_of_ten ( b.scale );
		__int128 a_whole = a.mantissa / a_divisor;
		__int128 b_whole = b.mantissa / b_divisor;

		if ( a_whole != b_whole )
			return a_whole < b_whole ? -1 : 1;

		uint32_t scale = base::max ( a.scale, b.scale );
		__int128 a_part = ( a.mantissa % a_divisor ) * power_of_ten ( scale - a.scale );
		__int128 b_part = ( b.mantissa % b_divisor ) * power_of_ten ( scale - b.scale );

		return a_part == b_part ? 0 : a_part < b_part ? -1 : 1;

	}

	bool decimal_add ( const decimal_number& a, const decimal_number& b, bool subtract, decimal_number& out ) {

		decimal_number x = a;
		decimal_number y = b;
		uint32_t scale = base::max ( a.scale, b.scale );

		if ( !decimal_rescale ( x, scale ) || !decimal_rescale ( y, scale ) )
			return false;

		out.mantissa = subtract ? x.mantissa - y.mantissa : x.mantissa + y.mantissa;
		out.scale = scale;

		return fits ( out.mantissa );

	}

	bool decimal_multiply ( const decimal_number& a, const decimal_number& b, decimal_number& out ) {

		decimal_unsigned x = magnitude ( a.mantissa );
		decimal_unsigned y = magnitude ( b.mantissa );
		uint32_t scale = a.scale + b.scale;

		if ( x && y ) {

			while ( digit_count ( x ) + digit_count ( y ) > DECIMAL_MAX_DIGITS + 1 && scale > 0 ) {

				if ( x % 10 == 0 && a.scale )
					x /= 10;
				else if ( y % 10 == 0 && b.scale )
					y /= 10;
				else
					return false;

				scale--;

			}

			if ( digit_count ( x ) + digit_count ( y ) > DECIMAL_MAX_DIGITS + 1 )
				return false;

		}

		decimal_unsigned product = x * y;

		if ( product > static_cast < decimal_unsigned > ( power_of_ten ( DECIMAL_MAX_DIGITS + 2 ) ) )
			return false;

		bool negative = ( a.mantissa < 0 ) != ( b.mantissa < 0 );

		out.mantissa = negative ? -static_cast < __int128 > ( product ) : static_cast < __int128 > ( product );
		out.scale = scale;

		if ( out.scale > DECIMAL_MAX_SCALE && !decimal_rescale ( out, DECIMAL_MAX_SCALE ) )
			return false;

		return fits ( out.mantissa );

	}

	static void leading_group ( const decimal_number& item, int32_t& weight, uint32_t& first ) {

		decimal_unsigned value = magnitude ( item.mantissa );
		char digits [ 48 ];
		uint32_t count = 0;

		do {

			digits [ count++ ] = static_cast < char > ( value % 10 );
			value /= 10;

		} while ( value );

		int32_t top = static_cast < int32_t > ( count ) - 1 - static_cast < int32_t > ( item.scale );

		weight = top >= 0 ? top / 4 : -( ( -top + 3 ) / 4 );
		first = 0;

		for ( int32_t exponent = 4 * weight + 3; exponent >= 4 * weight; exponent-- ) {

			int32_t index = exponent + static_cast < int32_t > ( item.scale );

			first = first * 10 + ( index >= 0 && index < static_cast < int32_t > ( count ) ? static_cast < uint32_t > ( digits [ index ] ) : 0 );

		}

	}

	bool decimal_divide ( const decimal_number& a, const decimal_number& b, decimal_number& out ) {

		if ( !b.mantissa )
			return false;

		uint32_t scale = base::max ( a.scale, b.scale );

		if ( a.mantissa ) {

			int32_t a_weight = 0;
			int32_t b_weight = 0;
			uint32_t a_first = 0;
			uint32_t b_first = 0;

			leading_group ( a, a_weight, a_first );
			leading_group ( b, b_weight, b_first );

			int32_t quotient_weight = a_weight - b_weight;

			if ( a_first <= b_first )
				quotient_weight--;

			int32_t wanted = DECIMAL_DIVIDE_DIGITS - quotient_weight * 4;

			if ( wanted > static_cast < int32_t > ( scale ) )
				scale = static_cast < uint32_t > ( wanted );

		}

		if ( scale > DECIMAL_MAX_SCALE )
			scale = DECIMAL_MAX_SCALE;

		decimal_unsigned numerator = magnitude ( a.mantissa );
		decimal_unsigned denominator = magnitude ( b.mantissa );
		int32_t shift = static_cast < int32_t > ( scale ) + static_cast < int32_t > ( b.scale ) - static_cast < int32_t > ( a.scale ) + 1;

		while ( shift < 0 ) {

			denominator *= 10;
			shift++;

			if ( denominator > static_cast < decimal_unsigned > ( power_of_ten ( DECIMAL_MAX_DIGITS + 1 ) ) )
				return false;

		}

		decimal_unsigned quotient = numerator / denominator;
		decimal_unsigned rest = numerator % denominator;

		if ( digit_count ( quotient ) > DECIMAL_MAX_DIGITS )
			return false;

		for ( int32_t i = 0; i < shift; i++ ) {

			rest *= 10;
			quotient = quotient * 10 + rest / denominator;
			rest %= denominator;

			if ( digit_count ( quotient ) > DECIMAL_MAX_DIGITS + 1 )
				return false;

		}

		uint32_t last = static_cast < uint32_t > ( quotient % 10 );

		quotient /= 10;

		if ( last >= 5 )
			quotient++;

		bool negative = ( a.mantissa < 0 ) != ( b.mantissa < 0 );

		out.mantissa = negative ? -static_cast < __int128 > ( quotient ) : static_cast < __int128 > ( quotient );
		out.scale = scale;

		return fits ( out.mantissa );

	}

	bool decimal_modulo ( const decimal_number& a, const decimal_number& b, decimal_number& out ) {

		decimal_number x = a;
		decimal_number y = b;
		uint32_t scale = base::max ( a.scale, b.scale );

		if ( !y.mantissa || !decimal_rescale ( x, scale ) || !decimal_rescale ( y, scale ) )
			return false;

		out.mantissa = x.mantissa % y.mantissa;
		out.scale = scale;

		return true;

	}

	bool decimal_round ( decimal_number& item, int32_t places ) {

		if ( places >= static_cast < int32_t > ( item.scale ) )
			return decimal_rescale ( item, places > DECIMAL_MAX_SCALE ? DECIMAL_MAX_SCALE : static_cast < uint32_t > ( places ) );

		if ( places >= 0 )
			return decimal_rescale ( item, static_cast < uint32_t > ( places ) );

		if ( !decimal_rescale ( item, 0 ) )
			return false;

		__int128 unit = power_of_ten ( static_cast < uint32_t > ( -places ) > 36 ? 36 : static_cast < uint32_t > ( -places ) );
		__int128 rest = item.mantissa % unit;

		item.mantissa -= rest;

		if ( magnitude ( rest ) * 2 >= static_cast < decimal_unsigned > ( unit ) )
			item.mantissa += item.mantissa < 0 || rest < 0 ? -unit : unit;

		return fits ( item.mantissa );

	}

	bool decimal_type ( const char* type, int32_t& precision, int32_t& scale ) {

		precision = -1;
		scale = -1;

		if ( !type )
			return false;

		bool exact = base::find_folded ( type, "numeric" ) || base::find_folded ( type, "decimal" ) || base::find_folded ( type, "money" );

		if ( !exact )
			return false;

		if ( base::find_folded ( type, "money" ) ) {

			scale = 2;

			return true;

		}

		const char* open = type;

		while ( *open && *open != '(' )
			open++;

		if ( !*open )
			return true;

		int32_t numbers [ 2 ] = { -1, -1 };
		uint32_t count = 0;

		for ( const char* at = open + 1; *at && *at != ')' && count < 2; at++ ) {

			if ( *at >= '0' && *at <= '9' ) {

				int32_t value = 0;

				while ( *at >= '0' && *at <= '9' )
					value = base::min ( value * 10 + ( *at++ - '0' ), 1000 );

				numbers [ count++ ] = value;
				at--;

			}

		}

		precision = numbers [ 0 ];
		scale = count > 1 ? numbers [ 1 ] : count == 1 ? 0 : -1;

		if ( scale > DECIMAL_MAX_SCALE )
			scale = DECIMAL_MAX_SCALE;

		return true;

	}

}
