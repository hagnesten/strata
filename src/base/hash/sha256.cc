#include "base/hash/sha256.hh"
#include "base/runtime/memory.hh"

namespace base {

	static const uint32_t rounds [ 64 ] = {

		0x428A2F98, 0x71374491, 0xB5C0FBCF, 0xE9B5DBA5, 0x3956C25B, 0x59F111F1, 0x923F82A4, 0xAB1C5ED5,
		0xD807AA98, 0x12835B01, 0x243185BE, 0x550C7DC3, 0x72BE5D74, 0x80DEB1FE, 0x9BDC06A7, 0xC19BF174,
		0xE49B69C1, 0xEFBE4786, 0x0FC19DC6, 0x240CA1CC, 0x2DE92C6F, 0x4A7484AA, 0x5CB0A9DC, 0x76F988DA,
		0x983E5152, 0xA831C66D, 0xB00327C8, 0xBF597FC7, 0xC6E00BF3, 0xD5A79147, 0x06CA6351, 0x14292967,
		0x27B70A85, 0x2E1B2138, 0x4D2C6DFC, 0x53380D13, 0x650A7354, 0x766A0ABB, 0x81C2C92E, 0x92722C85,
		0xA2BFE8A1, 0xA81A664B, 0xC24B8B70, 0xC76C51A3, 0xD192E819, 0xD6990624, 0xF40E3585, 0x106AA070,
		0x19A4C116, 0x1E376C08, 0x2748774C, 0x34B0BCB5, 0x391C0CB3, 0x4ED8AA4A, 0x5B9CCA4F, 0x682E6FF3,
		0x748F82EE, 0x78A5636F, 0x84C87814, 0x8CC70208, 0x90BEFFFA, 0xA4506CEB, 0xBEF9A3F7, 0xC67178F2,

	};

	static inline uint32_t turn ( uint32_t value, uint32_t by ) {

		return ( value >> by ) | ( value << ( 32 - by ) );

	}

	static void compress ( sha256_state& state, const uint8_t* block ) {

		uint32_t w [ 64 ];

		for ( uint32_t i = 0; i < 16; i++ )
			w [ i ] = static_cast < uint32_t > ( block [ i * 4 ] ) << 24 | static_cast < uint32_t > ( block [ i * 4 + 1 ] ) << 16 | static_cast < uint32_t > ( block [ i * 4 + 2 ] ) << 8 | block [ i * 4 + 3 ];

		for ( uint32_t i = 16; i < 64; i++ ) {

			uint32_t low = turn ( w [ i - 15 ], 7 ) ^ turn ( w [ i - 15 ], 18 ) ^ ( w [ i - 15 ] >> 3 );
			uint32_t high = turn ( w [ i - 2 ], 17 ) ^ turn ( w [ i - 2 ], 19 ) ^ ( w [ i - 2 ] >> 10 );

			w [ i ] = w [ i - 16 ] + low + w [ i - 7 ] + high;

		}

		uint32_t a = state.words [ 0 ];
		uint32_t b = state.words [ 1 ];
		uint32_t c = state.words [ 2 ];
		uint32_t d = state.words [ 3 ];
		uint32_t e = state.words [ 4 ];
		uint32_t f = state.words [ 5 ];
		uint32_t g = state.words [ 6 ];
		uint32_t h = state.words [ 7 ];

		for ( uint32_t i = 0; i < 64; i++ ) {

			uint32_t big_e = turn ( e, 6 ) ^ turn ( e, 11 ) ^ turn ( e, 25 );
			uint32_t choose = ( e & f ) ^ ( ~e & g );
			uint32_t first = h + big_e + choose + rounds [ i ] + w [ i ];
			uint32_t big_a = turn ( a, 2 ) ^ turn ( a, 13 ) ^ turn ( a, 22 );
			uint32_t major = ( a & b ) ^ ( a & c ) ^ ( b & c );
			uint32_t second = big_a + major;

			h = g;
			g = f;
			f = e;
			e = d + first;
			d = c;
			c = b;
			b = a;
			a = first + second;

		}

		state.words [ 0 ] += a;
		state.words [ 1 ] += b;
		state.words [ 2 ] += c;
		state.words [ 3 ] += d;
		state.words [ 4 ] += e;
		state.words [ 5 ] += f;
		state.words [ 6 ] += g;
		state.words [ 7 ] += h;

	}

	void sha256_start ( sha256_state& state ) {

		static const uint32_t first [ 8 ] = { 0x6A09E667, 0xBB67AE85, 0x3C6EF372, 0xA54FF53A, 0x510E527F, 0x9B05688C, 0x1F83D9AB, 0x5BE0CD19 };

		for ( uint32_t i = 0; i < 8; i++ )
			state.words [ i ] = first [ i ];

		state.used = 0;
		state.total = 0;

	}

	void sha256_add ( sha256_state& state, const void* data, size_t size ) {

		const uint8_t* at = static_cast < const uint8_t* > ( data );

		state.total += size;

		if ( state.used ) {

			size_t room = SHA256_BLOCK - state.used;
			size_t take = size < room ? size : room;

			copy ( state.block + state.used, at, take );
			state.used += static_cast < uint32_t > ( take );
			at += take;
			size -= take;

			if ( state.used < SHA256_BLOCK )
				return;

			compress ( state, state.block );
			state.used = 0;

		}

		while ( size >= SHA256_BLOCK ) {

			compress ( state, at );
			at += SHA256_BLOCK;
			size -= SHA256_BLOCK;

		}

		if ( size ) {

			copy ( state.block, at, size );
			state.used = static_cast < uint32_t > ( size );

		}

	}

	void sha256_finish ( sha256_state& state, uint8_t out [ SHA256_BYTES ] ) {

		uint64_t bits = state.total * 8;

		state.block [ state.used++ ] = 0x80;

		if ( state.used > SHA256_BLOCK - 8 ) {

			fill ( state.block + state.used, 0, SHA256_BLOCK - state.used );
			compress ( state, state.block );
			state.used = 0;

		}

		fill ( state.block + state.used, 0, SHA256_BLOCK - 8 - state.used );

		for ( uint32_t i = 0; i < 8; i++ )
			state.block [ SHA256_BLOCK - 1 - i ] = static_cast < uint8_t > ( bits >> ( i * 8 ) );

		compress ( state, state.block );

		for ( uint32_t i = 0; i < 8; i++ ) {

			out [ i * 4 ] = static_cast < uint8_t > ( state.words [ i ] >> 24 );
			out [ i * 4 + 1 ] = static_cast < uint8_t > ( state.words [ i ] >> 16 );
			out [ i * 4 + 2 ] = static_cast < uint8_t > ( state.words [ i ] >> 8 );
			out [ i * 4 + 3 ] = static_cast < uint8_t > ( state.words [ i ] );

		}

	}

	void sha256 ( const void* data, size_t size, uint8_t out [ SHA256_BYTES ] ) {

		sha256_state state;

		sha256_start ( state );
		sha256_add ( state, data, size );
		sha256_finish ( state, out );

	}

	void hmac_start ( hmac_state& state, const void* key, size_t size ) {

		uint8_t block [ SHA256_BLOCK ] = { };
		uint8_t pad [ SHA256_BLOCK ];

		if ( size > SHA256_BLOCK )
			sha256 ( key, size, block );
		else
			copy ( block, key, size );

		for ( uint32_t i = 0; i < SHA256_BLOCK; i++ )
			pad [ i ] = block [ i ] ^ 0x36;

		sha256_start ( state.inner );
		sha256_add ( state.inner, pad, SHA256_BLOCK );

		for ( uint32_t i = 0; i < SHA256_BLOCK; i++ )
			pad [ i ] = block [ i ] ^ 0x5C;

		sha256_start ( state.outer );
		sha256_add ( state.outer, pad, SHA256_BLOCK );

	}

	void hmac_add ( hmac_state& state, const void* data, size_t size ) {

		sha256_add ( state.inner, data, size );

	}

	void hmac_finish ( hmac_state& state, uint8_t out [ SHA256_BYTES ] ) {

		uint8_t inner [ SHA256_BYTES ];

		sha256_finish ( state.inner, inner );
		sha256_add ( state.outer, inner, SHA256_BYTES );
		sha256_finish ( state.outer, out );

	}

	void hmac_sha256 ( const void* key, size_t key_size, const void* data, size_t size, uint8_t out [ SHA256_BYTES ] ) {

		hmac_state state;

		hmac_start ( state, key, key_size );
		hmac_add ( state, data, size );
		hmac_finish ( state, out );

	}

	void pbkdf2_sha256 ( const void* password, size_t password_size, const void* salt, size_t salt_size, uint32_t count, uint8_t* out, size_t size ) {

		hmac_state keyed;

		hmac_start ( keyed, password, password_size );

		for ( uint32_t block = 1; size; block++ ) {

			uint8_t number [ 4 ] = { static_cast < uint8_t > ( block >> 24 ), static_cast < uint8_t > ( block >> 16 ), static_cast < uint8_t > ( block >> 8 ), static_cast < uint8_t > ( block ) };
			uint8_t step [ SHA256_BYTES ];
			uint8_t sum [ SHA256_BYTES ];
			hmac_state state = keyed;

			hmac_add ( state, salt, salt_size );
			hmac_add ( state, number, 4 );
			hmac_finish ( state, step );
			copy ( sum, step, SHA256_BYTES );

			for ( uint32_t i = 1; i < count; i++ ) {

				state = keyed;
				hmac_add ( state, step, SHA256_BYTES );
				hmac_finish ( state, step );

				for ( uint32_t k = 0; k < SHA256_BYTES; k++ )
					sum [ k ] ^= step [ k ];

			}

			size_t take = size < SHA256_BYTES ? size : SHA256_BYTES;

			copy ( out, sum, take );
			out += take;
			size -= take;

		}

	}

}
