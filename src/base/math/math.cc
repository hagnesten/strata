#include "base/math/math.hh"

namespace base {

	double natural_log ( double value ) {

		if ( value != value || value < 0 )
			return 0.0 / 0.0;

		if ( value == 0 )
			return -1.0 / 0.0;

		if ( value > 1.7976931348623157e308 )
			return value;

		uint64_t bits = 0;

		copy ( &bits, &value, sizeof ( bits ) );

		int32_t exponent = static_cast < int32_t > ( bits >> 52 & 0x7FF );

		if ( !exponent ) {

			value *= 4503599627370496.0;
			copy ( &bits, &value, sizeof ( bits ) );
			exponent = static_cast < int32_t > ( bits >> 52 & 0x7FF ) - 52;

		}

		exponent -= 1023;
		bits = ( bits & 0x000FFFFFFFFFFFFFull ) | 0x3FF0000000000000ull;

		double mantissa = 0;

		copy ( &mantissa, &bits, sizeof ( mantissa ) );

		if ( mantissa > 1.4142135623730951 ) {

			mantissa /= 2;
			exponent++;

		}

		double ratio = ( mantissa - 1 ) / ( mantissa + 1 );
		double square = ratio * ratio;
		double term = ratio;
		double sum = 0;

		for ( uint32_t k = 1; k < 60; k += 2 ) {

			sum += term / k;
			term *= square;

			if ( term < 1e-18 && term > -1e-18 )
				break;

		}

		return 2 * sum + exponent * MATH_LN2;

	}

}