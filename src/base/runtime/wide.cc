#include "base/runtime/memory.hh"

typedef unsigned __int128 wide_unsigned;

static uint32_t leading_zeros ( wide_unsigned value ) {

	uint64_t high = static_cast < uint64_t > ( value >> 64 );
	uint64_t low = static_cast < uint64_t > ( value );

	if ( high )
		return static_cast < uint32_t > ( __builtin_clzll ( high ) );

	return low ? 64 + static_cast < uint32_t > ( __builtin_clzll ( low ) ) : 128;

}

static wide_unsigned divide_wide ( wide_unsigned numerator, wide_unsigned denominator, wide_unsigned* remainder ) {

	if ( !denominator ) {

		if ( remainder )
			*remainder = 0;

		return 0;

	}

	uint64_t numerator_high = static_cast < uint64_t > ( numerator >> 64 );
	uint64_t denominator_high = static_cast < uint64_t > ( denominator >> 64 );

	if ( !numerator_high && !denominator_high ) {

		uint64_t low = static_cast < uint64_t > ( numerator );
		uint64_t divisor = static_cast < uint64_t > ( denominator );

		if ( remainder )
			*remainder = low % divisor;

		return low / divisor;

	}

	if ( denominator > numerator ) {

		if ( remainder )
			*remainder = numerator;

		return 0;

	}

	uint32_t shift = leading_zeros ( denominator ) - leading_zeros ( numerator );
	wide_unsigned divisor = denominator << shift;
	wide_unsigned quotient = 0;

	for ( uint32_t i = 0; i <= shift; i++ ) {

		quotient <<= 1;

		if ( numerator >= divisor ) {

			numerator -= divisor;
			quotient |= 1;

		}

		divisor >>= 1;

	}

	if ( remainder )
		*remainder = numerator;

	return quotient;

}

extern "C" {

	wide_unsigned __udivti3 ( wide_unsigned numerator, wide_unsigned denominator ) {

		return divide_wide ( numerator, denominator, nullptr );

	}

	wide_unsigned __umodti3 ( wide_unsigned numerator, wide_unsigned denominator ) {

		wide_unsigned rest = 0;

		divide_wide ( numerator, denominator, &rest );

		return rest;

	}

	__int128 __divti3 ( __int128 numerator, __int128 denominator ) {

		bool negative = ( numerator < 0 ) != ( denominator < 0 );
		wide_unsigned top = numerator < 0 ? static_cast < wide_unsigned > ( 0 ) - static_cast < wide_unsigned > ( numerator ) : static_cast < wide_unsigned > ( numerator );
		wide_unsigned bottom = denominator < 0 ? static_cast < wide_unsigned > ( 0 ) - static_cast < wide_unsigned > ( denominator ) : static_cast < wide_unsigned > ( denominator );
		wide_unsigned quotient = divide_wide ( top, bottom, nullptr );

		return negative ? static_cast < __int128 > ( static_cast < wide_unsigned > ( 0 ) - quotient ) : static_cast < __int128 > ( quotient );

	}

	__int128 __modti3 ( __int128 numerator, __int128 denominator ) {

		wide_unsigned top = numerator < 0 ? static_cast < wide_unsigned > ( 0 ) - static_cast < wide_unsigned > ( numerator ) : static_cast < wide_unsigned > ( numerator );
		wide_unsigned bottom = denominator < 0 ? static_cast < wide_unsigned > ( 0 ) - static_cast < wide_unsigned > ( denominator ) : static_cast < wide_unsigned > ( denominator );
		wide_unsigned rest = 0;

		divide_wide ( top, bottom, &rest );

		return numerator < 0 ? static_cast < __int128 > ( static_cast < wide_unsigned > ( 0 ) - rest ) : static_cast < __int128 > ( rest );

	}

	double __floattidf ( __int128 value ) {

		bool negative = value < 0;
		wide_unsigned magnitude = negative ? static_cast < wide_unsigned > ( 0 ) - static_cast < wide_unsigned > ( value ) : static_cast < wide_unsigned > ( value );
		double result = static_cast < double > ( static_cast < uint64_t > ( magnitude >> 64 ) ) * 18446744073709551616.0 + static_cast < double > ( static_cast < uint64_t > ( magnitude ) );

		return negative ? -result : result;

	}

	double __floatuntidf ( wide_unsigned value ) {

		return static_cast < double > ( static_cast < uint64_t > ( value >> 64 ) ) * 18446744073709551616.0 + static_cast < double > ( static_cast < uint64_t > ( value ) );

	}

}
