#include "base/crypto/x25519.hh"

namespace base {

	typedef unsigned __int128 wide;
	typedef x25519_field field;

	static const uint64_t mask51 = ( 1ull << 51 ) - 1;

	static void field_set ( field& out, uint64_t value ) {

		out.limb [ 0 ] = value;

		for ( int i = 1; i < 5; i++ )
			out.limb [ i ] = 0;

	}

	static void field_add ( field& out, const field& a, const field& b ) {

		for ( int i = 0; i < 5; i++ )
			out.limb [ i ] = a.limb [ i ] + b.limb [ i ];

	}

	static void field_sub ( field& out, const field& a, const field& b ) {

		out.limb [ 0 ] = a.limb [ 0 ] + 0xFFFFFFFFFFFDAull - b.limb [ 0 ];

		for ( int i = 1; i < 5; i++ )
			out.limb [ i ] = a.limb [ i ] + 0xFFFFFFFFFFFFEull - b.limb [ i ];

	}

	static void field_carry ( field& out, wide t [ 5 ] ) {

		t [ 1 ] += static_cast < uint64_t > ( t [ 0 ] >> 51 );
		t [ 0 ] &= mask51;
		t [ 2 ] += static_cast < uint64_t > ( t [ 1 ] >> 51 );
		t [ 1 ] &= mask51;
		t [ 3 ] += static_cast < uint64_t > ( t [ 2 ] >> 51 );
		t [ 2 ] &= mask51;
		t [ 4 ] += static_cast < uint64_t > ( t [ 3 ] >> 51 );
		t [ 3 ] &= mask51;
		t [ 0 ] += 19 * static_cast < uint64_t > ( t [ 4 ] >> 51 );
		t [ 4 ] &= mask51;

		uint64_t c = static_cast < uint64_t > ( t [ 0 ] ) >> 51;

		out.limb [ 0 ] = static_cast < uint64_t > ( t [ 0 ] ) & mask51;
		out.limb [ 1 ] = static_cast < uint64_t > ( t [ 1 ] ) + c;
		out.limb [ 2 ] = static_cast < uint64_t > ( t [ 2 ] );
		out.limb [ 3 ] = static_cast < uint64_t > ( t [ 3 ] );
		out.limb [ 4 ] = static_cast < uint64_t > ( t [ 4 ] );

	}

	static void field_mul ( field& out, const field& a, const field& b ) {

		const uint64_t* x = a.limb;
		const uint64_t* y = b.limb;
		uint64_t y1 = y [ 1 ] * 19, y2 = y [ 2 ] * 19, y3 = y [ 3 ] * 19, y4 = y [ 4 ] * 19;
		wide t [ 5 ];

		t [ 0 ] = ( wide ) x [ 0 ] * y [ 0 ] + ( wide ) x [ 1 ] * y4 + ( wide ) x [ 2 ] * y3 + ( wide ) x [ 3 ] * y2 + ( wide ) x [ 4 ] * y1;
		t [ 1 ] = ( wide ) x [ 0 ] * y [ 1 ] + ( wide ) x [ 1 ] * y [ 0 ] + ( wide ) x [ 2 ] * y4 + ( wide ) x [ 3 ] * y3 + ( wide ) x [ 4 ] * y2;
		t [ 2 ] = ( wide ) x [ 0 ] * y [ 2 ] + ( wide ) x [ 1 ] * y [ 1 ] + ( wide ) x [ 2 ] * y [ 0 ] + ( wide ) x [ 3 ] * y4 + ( wide ) x [ 4 ] * y3;
		t [ 3 ] = ( wide ) x [ 0 ] * y [ 3 ] + ( wide ) x [ 1 ] * y [ 2 ] + ( wide ) x [ 2 ] * y [ 1 ] + ( wide ) x [ 3 ] * y [ 0 ] + ( wide ) x [ 4 ] * y4;
		t [ 4 ] = ( wide ) x [ 0 ] * y [ 4 ] + ( wide ) x [ 1 ] * y [ 3 ] + ( wide ) x [ 2 ] * y [ 2 ] + ( wide ) x [ 3 ] * y [ 1 ] + ( wide ) x [ 4 ] * y [ 0 ];

		field_carry ( out, t );

	}

	static void field_mul_small ( field& out, const field& a, uint64_t small ) {

		wide t [ 5 ];

		for ( int i = 0; i < 5; i++ )
			t [ i ] = ( wide ) a.limb [ i ] * small;

		field_carry ( out, t );

	}

	static void field_square ( field& out, const field& a ) {

		field_mul ( out, a, a );

	}

	static void field_invert ( field& out, const field& a ) {

		field result;
		field base_value = a;

		field_set ( result, 1 );

		for ( int bit = 254; bit >= 0; bit-- ) {

			field_square ( result, result );

			bool set = bit != 2 && bit != 4;

			if ( set )
				field_mul ( result, result, base_value );

		}

		out = result;

	}

	static void field_swap ( field& a, field& b, uint64_t swap ) {

		uint64_t mask = 0 - swap;

		for ( int i = 0; i < 5; i++ ) {

			uint64_t x = mask & ( a.limb [ i ] ^ b.limb [ i ] );

			a.limb [ i ] ^= x;
			b.limb [ i ] ^= x;

		}

	}

	static void field_read ( field& out, const uint8_t bytes [ 32 ] ) {

		uint64_t words [ 4 ];

		for ( int i = 0; i < 4; i++ ) {

			words [ i ] = 0;

			for ( int k = 7; k >= 0; k-- )
				words [ i ] = words [ i ] << 8 | bytes [ i * 8 + k ];

		}

		words [ 3 ] &= 0x7FFFFFFFFFFFFFFFull;

		out.limb [ 0 ] = words [ 0 ] & mask51;
		out.limb [ 1 ] = ( words [ 0 ] >> 51 | words [ 1 ] << 13 ) & mask51;
		out.limb [ 2 ] = ( words [ 1 ] >> 38 | words [ 2 ] << 26 ) & mask51;
		out.limb [ 3 ] = ( words [ 2 ] >> 25 | words [ 3 ] << 39 ) & mask51;
		out.limb [ 4 ] = words [ 3 ] >> 12;

	}

	static void field_write ( uint8_t bytes [ 32 ], const field& a ) {

		wide t [ 5 ];
		field f;

		for ( int i = 0; i < 5; i++ )
			t [ i ] = a.limb [ i ];

		field_carry ( f, t );

		for ( int i = 0; i < 5; i++ )
			t [ i ] = f.limb [ i ];

		field_carry ( f, t );

		uint64_t q = ( f.limb [ 0 ] + 19 ) >> 51;

		q = ( f.limb [ 1 ] + q ) >> 51;
		q = ( f.limb [ 2 ] + q ) >> 51;
		q = ( f.limb [ 3 ] + q ) >> 51;
		q = ( f.limb [ 4 ] + q ) >> 51;

		f.limb [ 0 ] += 19 * q;

		f.limb [ 1 ] += f.limb [ 0 ] >> 51;
		f.limb [ 0 ] &= mask51;
		f.limb [ 2 ] += f.limb [ 1 ] >> 51;
		f.limb [ 1 ] &= mask51;
		f.limb [ 3 ] += f.limb [ 2 ] >> 51;
		f.limb [ 2 ] &= mask51;
		f.limb [ 4 ] += f.limb [ 3 ] >> 51;
		f.limb [ 3 ] &= mask51;
		f.limb [ 4 ] &= mask51;

		uint64_t words [ 4 ];

		words [ 0 ] = f.limb [ 0 ] | f.limb [ 1 ] << 51;
		words [ 1 ] = f.limb [ 1 ] >> 13 | f.limb [ 2 ] << 38;
		words [ 2 ] = f.limb [ 2 ] >> 26 | f.limb [ 3 ] << 25;
		words [ 3 ] = f.limb [ 3 ] >> 39 | f.limb [ 4 ] << 12;

		for ( int i = 0; i < 4; i++ )
			for ( int k = 0; k < 8; k++ )
				bytes [ i * 8 + k ] = static_cast < uint8_t > ( words [ i ] >> ( 8 * k ) );

	}

	bool x25519 ( const uint8_t scalar [ X25519_BYTES ], const uint8_t point [ X25519_BYTES ], uint8_t out [ X25519_BYTES ] ) {

		uint8_t k [ 32 ];

		for ( int i = 0; i < 32; i++ )
			k [ i ] = scalar [ i ];

		k [ 0 ] &= 248;
		k [ 31 ] &= 127;
		k [ 31 ] |= 64;

		field x1, x2, z2, x3, z3, a, aa, b, bb, e, c, d, da, cb, t;

		field_read ( x1, point );
		field_set ( x2, 1 );
		field_set ( z2, 0 );
		x3 = x1;
		field_set ( z3, 1 );

		uint64_t swap = 0;

		for ( int bit = 254; bit >= 0; bit-- ) {

			uint64_t k_t = ( k [ bit >> 3 ] >> ( bit & 7 ) ) & 1;

			swap ^= k_t;
			field_swap ( x2, x3, swap );
			field_swap ( z2, z3, swap );
			swap = k_t;

			field_add ( a, x2, z2 );
			field_square ( aa, a );
			field_sub ( b, x2, z2 );
			field_square ( bb, b );
			field_sub ( e, aa, bb );
			field_add ( c, x3, z3 );
			field_sub ( d, x3, z3 );
			field_mul ( da, d, a );
			field_mul ( cb, c, b );
			field_add ( t, da, cb );
			field_square ( x3, t );
			field_sub ( t, da, cb );
			field_square ( t, t );
			field_mul ( z3, x1, t );
			field_mul ( x2, aa, bb );
			field_mul_small ( t, e, 121665 );
			field_add ( t, aa, t );
			field_mul ( z2, e, t );

		}

		field_swap ( x2, x3, swap );
		field_swap ( z2, z3, swap );
		field_invert ( z2, z2 );
		field_mul ( x2, x2, z2 );
		field_write ( out, x2 );

		uint8_t any = 0;

		for ( int i = 0; i < 32; i++ ) {

			any |= out [ i ];
			k [ i ] = 0;

		}

		return any != 0;

	}

	void x25519_public ( const uint8_t scalar [ X25519_BYTES ], uint8_t out [ X25519_BYTES ] ) {

		uint8_t base_point [ 32 ] = { 9 };

		x25519 ( scalar, base_point, out );

	}

}
