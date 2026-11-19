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

	static double power_of_two ( int64_t exponent ) {

		uint64_t bits = static_cast < uint64_t > ( exponent + 1023 ) << 52;
		double out = 0;

		copy ( &out, &bits, sizeof ( out ) );

		return out;

	}

	double natural_exp ( double value ) {

		if ( value != value )
			return value;

		if ( value > 709.782712893384 )
			return 1.0 / 0.0;

		if ( value < -745.1332191019412 )
			return 0;

		double scaled = value / MATH_LN2;
		int64_t whole = static_cast < int64_t > ( scaled >= 0 ? scaled + 0.5 : scaled - 0.5 );
		double rest = ( value - whole * 6.93147180369123816490e-01 ) - whole * 1.90821492927058770002e-10;
		double term = 1;
		double sum = 1;

		for ( uint32_t k = 1; k < 40; k++ ) {

			term *= rest / k;
			sum += term;

			if ( term < 1e-18 && term > -1e-18 )
				break;

		}

		if ( whole >= -1022 && whole <= 1023 )
			return sum * power_of_two ( whole );

		int64_t half = whole / 2;

		return sum * power_of_two ( half ) * power_of_two ( whole - half );

	}

}