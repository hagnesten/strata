#include "base/crypto/p256.hh"
#include "base/hash/sha256.hh"
#include "base/platform/os.hh"

namespace base {

	typedef unsigned __int128 wide;
	typedef p256_modulus modulus;
	typedef p256_point point;

	static uint64_t add_words ( uint64_t out [ 4 ], const uint64_t a [ 4 ], const uint64_t b [ 4 ] ) {

		wide carry = 0;

		for ( int i = 0; i < 4; i++ ) {

			carry += ( wide ) a [ i ] + b [ i ];
			out [ i ] = static_cast < uint64_t > ( carry );
			carry >>= 64;

		}

		return static_cast < uint64_t > ( carry );

	}

	static uint64_t sub_words ( uint64_t out [ 4 ], const uint64_t a [ 4 ], const uint64_t b [ 4 ] ) {

		uint64_t borrow = 0;

		for ( int i = 0; i < 4; i++ ) {

			wide difference = ( wide ) a [ i ] - b [ i ] - borrow;

			out [ i ] = static_cast < uint64_t > ( difference );
			borrow = static_cast < uint64_t > ( difference >> 64 ) & 1;

		}

		return borrow;

	}

	static void select_words ( uint64_t out [ 4 ], const uint64_t a [ 4 ], const uint64_t b [ 4 ], uint64_t pick_b ) {

		uint64_t mask = 0 - pick_b;

		for ( int i = 0; i < 4; i++ )
			out [ i ] = ( a [ i ] & ~mask ) | ( b [ i ] & mask );

	}

	static void mod_add ( const modulus& m, uint64_t out [ 4 ], const uint64_t a [ 4 ], const uint64_t b [ 4 ] ) {

		uint64_t sum [ 4 ];
		uint64_t reduced [ 4 ];
		uint64_t carry = add_words ( sum, a, b );
		uint64_t borrow = sub_words ( reduced, sum, m.m );

		select_words ( out, reduced, sum, borrow & ( carry ^ 1 ) );

	}

	static void mod_sub ( const modulus& m, uint64_t out [ 4 ], const uint64_t a [ 4 ], const uint64_t b [ 4 ] ) {

		uint64_t difference [ 4 ];
		uint64_t fixed [ 4 ];
		uint64_t borrow = sub_words ( difference, a, b );

		add_words ( fixed, difference, m.m );
		select_words ( out, difference, fixed, borrow );

	}

	static void mod_mul ( const modulus& m, uint64_t out [ 4 ], const uint64_t a [ 4 ], const uint64_t b [ 4 ] ) {

		uint64_t t [ 6 ] = { 0, 0, 0, 0, 0, 0 };

		for ( int i = 0; i < 4; i++ ) {

			wide carry = 0;

			for ( int j = 0; j < 4; j++ ) {

				carry += ( wide ) a [ j ] * b [ i ] + t [ j ];
				t [ j ] = static_cast < uint64_t > ( carry );
				carry >>= 64;

			}

			carry += t [ 4 ];
			t [ 4 ] = static_cast < uint64_t > ( carry );
			t [ 5 ] = static_cast < uint64_t > ( carry >> 64 );

			uint64_t factor = t [ 0 ] * m.inverse;

			carry = ( wide ) factor * m.m [ 0 ] + t [ 0 ];
			carry >>= 64;

			for ( int j = 1; j < 4; j++ ) {

				carry += ( wide ) factor * m.m [ j ] + t [ j ];
				t [ j - 1 ] = static_cast < uint64_t > ( carry );
				carry >>= 64;

			}

			carry += t [ 4 ];
			t [ 3 ] = static_cast < uint64_t > ( carry );
			t [ 4 ] = t [ 5 ] + static_cast < uint64_t > ( carry >> 64 );

		}

		uint64_t reduced [ 4 ];
		uint64_t borrow = sub_words ( reduced, t, m.m );

		select_words ( out, reduced, t, borrow & ( t [ 4 ] ^ 1 ) );

	}

	static void mod_pow ( const modulus& m, uint64_t out [ 4 ], const uint64_t a [ 4 ], const uint64_t exponent [ 4 ] ) {

		uint64_t result [ 4 ];
		uint64_t product [ 4 ];

		for ( int i = 0; i < 4; i++ )
			result [ i ] = m.one [ i ];

		for ( int bit = 255; bit >= 0; bit-- ) {

			mod_mul ( m, result, result, result );
			mod_mul ( m, product, result, a );
			select_words ( result, result, product, ( exponent [ bit >> 6 ] >> ( bit & 63 ) ) & 1 );

		}

		for ( int i = 0; i < 4; i++ )
			out [ i ] = result [ i ];

	}

	static void mod_invert ( const modulus& m, uint64_t out [ 4 ], const uint64_t a [ 4 ] ) {

		uint64_t two [ 4 ] = { 2, 0, 0, 0 };
		uint64_t exponent [ 4 ];

		sub_words ( exponent, m.m, two );
		mod_pow ( m, out, a, exponent );

	}

	static void to_mont ( const modulus& m, uint64_t out [ 4 ], const uint64_t a [ 4 ] ) {

		mod_mul ( m, out, a, m.square );

	}

	static void from_mont ( const modulus& m, uint64_t out [ 4 ], const uint64_t a [ 4 ] ) {

		uint64_t unit [ 4 ] = { 1, 0, 0, 0 };

		mod_mul ( m, out, a, unit );

	}

	static void setup ( modulus& m, uint64_t m0, uint64_t m1, uint64_t m2, uint64_t m3, uint64_t inverse ) {

		m.m [ 0 ] = m0;
		m.m [ 1 ] = m1;
		m.m [ 2 ] = m2;
		m.m [ 3 ] = m3;
		m.inverse = inverse;

		uint64_t zero [ 4 ] = { 0, 0, 0, 0 };

		sub_words ( m.one, zero, m.m );

		for ( int i = 0; i < 4; i++ )
			m.square [ i ] = m.one [ i ];

		for ( int i = 0; i < 256; i++ )
			mod_add ( m, m.square, m.square, m.square );

	}

	static modulus field_p;
	static modulus field_n;
	static uint64_t curve_b [ 4 ];
	static point generator;
	static bool ready = false;

	static void read_be ( uint64_t out [ 4 ], const uint8_t bytes [ 32 ] ) {

		for ( int i = 0; i < 4; i++ ) {

			out [ i ] = 0;

			for ( int k = 0; k < 8; k++ )
				out [ i ] = out [ i ] << 8 | bytes [ ( 3 - i ) * 8 + k ];

		}

	}

	static void write_be ( uint8_t bytes [ 32 ], const uint64_t a [ 4 ] ) {

		for ( int i = 0; i < 4; i++ )
			for ( int k = 0; k < 8; k++ )
				bytes [ ( 3 - i ) * 8 + k ] = static_cast < uint8_t > ( a [ i ] >> ( 56 - 8 * k ) );

	}

	static void prepare ( ) {

		if ( ready )
			return;

		setup ( field_p, 0xFFFFFFFFFFFFFFFFull, 0x00000000FFFFFFFFull, 0x0000000000000000ull, 0xFFFFFFFF00000001ull, 1 );
		setup ( field_n, 0xF3B9CAC2FC632551ull, 0xBCE6FAADA7179E84ull, 0xFFFFFFFFFFFFFFFFull, 0xFFFFFFFF00000000ull, 0xCCD1C8AAEE00BC4Full );

		uint64_t b [ 4 ] = { 0x3BCE3C3E27D2604Bull, 0x651D06B0CC53B0F6ull, 0xB3EBBD55769886BCull, 0x5AC635D8AA3A93E7ull };
		uint64_t gx [ 4 ] = { 0xF4A13945D898C296ull, 0x77037D812DEB33A0ull, 0xF8BCE6E563A440F2ull, 0x6B17D1F2E12C4247ull };
		uint64_t gy [ 4 ] = { 0xCBB6406837BF51F5ull, 0x2BCE33576B315ECEull, 0x8EE7EB4A7C0F9E16ull, 0x4FE342E2FE1A7F9Bull };

		to_mont ( field_p, curve_b, b );
		to_mont ( field_p, generator.x, gx );
		to_mont ( field_p, generator.y, gy );

		for ( int i = 0; i < 4; i++ )
			generator.z [ i ] = field_p.one [ i ];

		ready = true;

	}

	static void point_add ( point& out, const point& p, const point& q ) {

		const modulus& f = field_p;
		uint64_t t0 [ 4 ], t1 [ 4 ], t2 [ 4 ], t3 [ 4 ], t4 [ 4 ], x3 [ 4 ], y3 [ 4 ], z3 [ 4 ];

		mod_mul ( f, t0, p.x, q.x );
		mod_mul ( f, t1, p.y, q.y );
		mod_mul ( f, t2, p.z, q.z );
		mod_add ( f, t3, p.x, p.y );
		mod_add ( f, t4, q.x, q.y );
		mod_mul ( f, t3, t3, t4 );
		mod_add ( f, t4, t0, t1 );
		mod_sub ( f, t3, t3, t4 );
		mod_add ( f, t4, p.y, p.z );
		mod_add ( f, x3, q.y, q.z );
		mod_mul ( f, t4, t4, x3 );
		mod_add ( f, x3, t1, t2 );
		mod_sub ( f, t4, t4, x3 );
		mod_add ( f, x3, p.x, p.z );
		mod_add ( f, y3, q.x, q.z );
		mod_mul ( f, x3, x3, y3 );
		mod_add ( f, y3, t0, t2 );
		mod_sub ( f, y3, x3, y3 );
		mod_mul ( f, z3, curve_b, t2 );
		mod_sub ( f, x3, y3, z3 );
		mod_add ( f, z3, x3, x3 );
		mod_add ( f, x3, x3, z3 );
		mod_sub ( f, z3, t1, x3 );
		mod_add ( f, x3, t1, x3 );
		mod_mul ( f, y3, curve_b, y3 );
		mod_add ( f, t1, t2, t2 );
		mod_add ( f, t2, t1, t2 );
		mod_sub ( f, y3, y3, t2 );
		mod_sub ( f, y3, y3, t0 );
		mod_add ( f, t1, y3, y3 );
		mod_add ( f, y3, t1, y3 );
		mod_add ( f, t1, t0, t0 );
		mod_add ( f, t0, t1, t0 );
		mod_sub ( f, t0, t0, t2 );
		mod_mul ( f, t1, t4, y3 );
		mod_mul ( f, t2, t0, y3 );
		mod_mul ( f, y3, x3, z3 );
		mod_add ( f, y3, y3, t2 );
		mod_mul ( f, x3, t3, x3 );
		mod_sub ( f, x3, x3, t1 );
		mod_mul ( f, z3, t4, z3 );
		mod_mul ( f, t1, t3, t0 );
		mod_add ( f, z3, z3, t1 );

		for ( int i = 0; i < 4; i++ ) {

			out.x [ i ] = x3 [ i ];
			out.y [ i ] = y3 [ i ];
			out.z [ i ] = z3 [ i ];

		}

	}

	static void point_infinity ( point& out ) {

		for ( int i = 0; i < 4; i++ ) {

			out.x [ i ] = 0;
			out.y [ i ] = field_p.one [ i ];
			out.z [ i ] = 0;

		}

	}

	static void point_select ( point& out, const point& a, const point& b, uint64_t pick_b ) {

		select_words ( out.x, a.x, b.x, pick_b );
		select_words ( out.y, a.y, b.y, pick_b );
		select_words ( out.z, a.z, b.z, pick_b );

	}

	static void point_multiply ( point& out, const point& p, const uint64_t scalar [ 4 ] ) {

		point result;
		point sum;

		point_infinity ( result );

		for ( int bit = 255; bit >= 0; bit-- ) {

			point_add ( result, result, result );
			point_add ( sum, result, p );
			point_select ( result, result, sum, ( scalar [ bit >> 6 ] >> ( bit & 63 ) ) & 1 );

		}

		out = result;

	}

	static bool point_affine ( const point& p, uint64_t x [ 4 ], uint64_t y [ 4 ] ) {

		uint64_t inverse [ 4 ];
		uint64_t zero = 0;

		for ( int i = 0; i < 4; i++ )
			zero |= p.z [ i ];

		if ( !zero )
			return false;

		mod_invert ( field_p, inverse, p.z );
		mod_mul ( field_p, x, p.x, inverse );
		mod_mul ( field_p, y, p.y, inverse );
		from_mont ( field_p, x, x );
		from_mont ( field_p, y, y );

		return true;

	}

	static bool scalar_valid ( const uint64_t k [ 4 ] ) {

		uint64_t scratch [ 4 ];
		uint64_t any = k [ 0 ] | k [ 1 ] | k [ 2 ] | k [ 3 ];

		return any && sub_words ( scratch, k, field_n.m );

	}

	static void reduce_once ( uint64_t value [ 4 ] ) {

		uint64_t reduced [ 4 ];
		uint64_t borrow = sub_words ( reduced, value, field_n.m );

		select_words ( value, reduced, value, borrow );

	}

	bool p256_public ( const uint8_t secret [ P256_SCALAR_BYTES ], uint8_t out [ P256_POINT_BYTES ] ) {

		prepare ( );

		uint64_t d [ 4 ];
		uint64_t x [ 4 ];
		uint64_t y [ 4 ];
		point q;

		read_be ( d, secret );

		if ( !scalar_valid ( d ) )
			return false;

		point_multiply ( q, generator, d );

		if ( !point_affine ( q, x, y ) )
			return false;

		out [ 0 ] = 4;
		write_be ( out + 1, x );
		write_be ( out + 33, y );

		return true;

	}

	static void nonce ( const uint8_t secret [ 32 ], const uint8_t digest [ 32 ], uint64_t k [ 4 ] ) {

		uint8_t v [ 32 ];
		uint8_t key [ 32 ];
		uint8_t reduced [ 32 ];
		uint64_t e [ 4 ];
		hmac_state mac;

		read_be ( e, digest );
		reduce_once ( e );
		write_be ( reduced, e );

		for ( int i = 0; i < 32; i++ ) {

			v [ i ] = 1;
			key [ i ] = 0;

		}

		for ( uint8_t round = 0; round < 2; round++ ) {

			hmac_start ( mac, key, 32 );
			hmac_add ( mac, v, 32 );
			hmac_add ( mac, &round, 1 );
			hmac_add ( mac, secret, 32 );
			hmac_add ( mac, reduced, 32 );
			hmac_finish ( mac, key );
			hmac_sha256 ( key, 32, v, 32, v );

		}

		for ( ;; ) {

			hmac_sha256 ( key, 32, v, 32, v );
			read_be ( k, v );

			if ( scalar_valid ( k ) )
				break;

			uint8_t zero = 0;

			hmac_start ( mac, key, 32 );
			hmac_add ( mac, v, 32 );
			hmac_add ( mac, &zero, 1 );
			hmac_finish ( mac, key );
			hmac_sha256 ( key, 32, v, 32, v );

		}

		for ( int i = 0; i < 32; i++ ) {

			v [ i ] = 0;
			key [ i ] = 0;

		}

	}

	bool p256_sign ( const uint8_t secret [ P256_SCALAR_BYTES ], const uint8_t digest [ 32 ], uint8_t r_out [ 32 ], uint8_t s_out [ 32 ] ) {

		prepare ( );

		uint64_t d [ 4 ], e [ 4 ], k [ 4 ], x [ 4 ], y [ 4 ], r [ 4 ], s [ 4 ];
		uint64_t dm [ 4 ], em [ 4 ], km [ 4 ], rm [ 4 ], kinv [ 4 ], sum [ 4 ];
		point kg;

		read_be ( d, secret );

		if ( !scalar_valid ( d ) )
			return false;

		read_be ( e, digest );
		reduce_once ( e );
		nonce ( secret, digest, k );
		point_multiply ( kg, generator, k );

		if ( !point_affine ( kg, x, y ) )
			return false;

		for ( int i = 0; i < 4; i++ )
			r [ i ] = x [ i ];

		reduce_once ( r );

		if ( !( r [ 0 ] | r [ 1 ] | r [ 2 ] | r [ 3 ] ) )
			return false;

		to_mont ( field_n, dm, d );
		to_mont ( field_n, em, e );
		to_mont ( field_n, km, k );
		to_mont ( field_n, rm, r );
		mod_invert ( field_n, kinv, km );
		mod_mul ( field_n, sum, rm, dm );
		mod_add ( field_n, sum, sum, em );
		mod_mul ( field_n, s, kinv, sum );
		from_mont ( field_n, s, s );

		if ( !( s [ 0 ] | s [ 1 ] | s [ 2 ] | s [ 3 ] ) )
			return false;

		write_be ( r_out, r );
		write_be ( s_out, s );

		for ( int i = 0; i < 4; i++ )
			d [ i ] = k [ i ] = dm [ i ] = km [ i ] = kinv [ i ] = 0;

		return true;

	}

	static bool on_curve ( const uint64_t x [ 4 ], const uint64_t y [ 4 ] ) {

		uint64_t left [ 4 ], right [ 4 ], three_x [ 4 ];

		mod_mul ( field_p, left, y, y );
		mod_mul ( field_p, right, x, x );
		mod_mul ( field_p, right, right, x );
		mod_add ( field_p, three_x, x, x );
		mod_add ( field_p, three_x, three_x, x );
		mod_sub ( field_p, right, right, three_x );
		mod_add ( field_p, right, right, curve_b );

		uint64_t difference = 0;

		for ( int i = 0; i < 4; i++ )
			difference |= left [ i ] ^ right [ i ];

		return !difference;

	}

	bool p256_verify ( const uint8_t encoded [ P256_POINT_BYTES ], const uint8_t digest [ 32 ], const uint8_t r_bytes [ 32 ], const uint8_t s_bytes [ 32 ] ) {

		prepare ( );

		if ( encoded [ 0 ] != 4 )
			return false;

		uint64_t qx [ 4 ], qy [ 4 ], r [ 4 ], s [ 4 ], e [ 4 ], scratch [ 4 ];

		read_be ( qx, encoded + 1 );
		read_be ( qy, encoded + 33 );
		read_be ( r, r_bytes );
		read_be ( s, s_bytes );

		if ( !sub_words ( scratch, qx, field_p.m ) || !sub_words ( scratch, qy, field_p.m ) || !scalar_valid ( r ) || !scalar_valid ( s ) )
			return false;

		point q;

		to_mont ( field_p, q.x, qx );
		to_mont ( field_p, q.y, qy );

		for ( int i = 0; i < 4; i++ )
			q.z [ i ] = field_p.one [ i ];

		if ( !on_curve ( q.x, q.y ) )
			return false;

		read_be ( e, digest );
		reduce_once ( e );

		uint64_t sm [ 4 ], w [ 4 ], u1 [ 4 ], u2 [ 4 ], em [ 4 ], rm [ 4 ];

		to_mont ( field_n, sm, s );
		to_mont ( field_n, em, e );
		to_mont ( field_n, rm, r );
		mod_invert ( field_n, w, sm );
		mod_mul ( field_n, u1, em, w );
		mod_mul ( field_n, u2, rm, w );
		from_mont ( field_n, u1, u1 );
		from_mont ( field_n, u2, u2 );

		point a, b, sum;
		uint64_t x [ 4 ], y [ 4 ];

		point_multiply ( a, generator, u1 );
		point_multiply ( b, q, u2 );
		point_add ( sum, a, b );

		if ( !point_affine ( sum, x, y ) )
			return false;

		reduce_once ( x );

		uint64_t difference = 0;

		for ( int i = 0; i < 4; i++ )
			difference |= x [ i ] ^ r [ i ];

		return !difference;

	}

	bool p256_new_secret ( uint8_t out [ P256_SCALAR_BYTES ] ) {

		prepare ( );

		for ( int tries = 0; tries < 64; tries++ ) {

			uint64_t d [ 4 ];

			if ( !os::random_bytes ( out, P256_SCALAR_BYTES ) )
				return false;

			read_be ( d, out );

			if ( scalar_valid ( d ) )
				return true;

		}

		return false;

	}

}
