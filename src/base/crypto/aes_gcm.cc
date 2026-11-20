#include "base/crypto/aes_gcm.hh"

#include <wmmintrin.h>
#include <tmmintrin.h>

#define CRYPTO_TARGET __attribute__ ( ( target ( "aes,pclmul,ssse3,sse4.1" ) ) )

namespace base {

	static void cpu_id ( uint32_t leaf, uint32_t out [ 4 ] ) {

		uint32_t a = 0, b = 0, c = 0, d = 0;

		__asm__ volatile ( "cpuid" : "=a" ( a ), "=b" ( b ), "=c" ( c ), "=d" ( d ) : "a" ( leaf ), "c" ( 0 ) );

		out [ 0 ] = a;
		out [ 1 ] = b;
		out [ 2 ] = c;
		out [ 3 ] = d;

	}

	bool aes_gcm_supported ( ) {

		uint32_t registers [ 4 ];

		cpu_id ( 1, registers );

		uint32_t c = registers [ 2 ];

		return ( c & ( 1u << 25 ) ) && ( c & ( 1u << 1 ) ) && ( c & ( 1u << 9 ) ) && ( c & ( 1u << 19 ) );

	}

	CRYPTO_TARGET static __m128i expand_step ( __m128i key, __m128i generated ) {

		generated = _mm_shuffle_epi32 ( generated, 0xFF );
		key = _mm_xor_si128 ( key, _mm_slli_si128 ( key, 4 ) );
		key = _mm_xor_si128 ( key, _mm_slli_si128 ( key, 4 ) );
		key = _mm_xor_si128 ( key, _mm_slli_si128 ( key, 4 ) );

		return _mm_xor_si128 ( key, generated );

	}

	CRYPTO_TARGET static __m128i encrypt_block ( const aes_gcm_key& key, __m128i block ) {

		const __m128i* rounds = reinterpret_cast < const __m128i* > ( key.rounds );

		block = _mm_xor_si128 ( block, _mm_load_si128 ( rounds ) );

		for ( int i = 1; i < 10; i++ )
			block = _mm_aesenc_si128 ( block, _mm_load_si128 ( rounds + i ) );

		return _mm_aesenclast_si128 ( block, _mm_load_si128 ( rounds + 10 ) );

	}

	CRYPTO_TARGET static __m128i reverse ( __m128i value ) {

		const __m128i order = _mm_set_epi8 ( 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 );

		return _mm_shuffle_epi8 ( value, order );

	}

	CRYPTO_TARGET static __m128i multiply ( __m128i a, __m128i b ) {

		__m128i t2, t3, t4, t5, t6, t7, t8, t9;

		t3 = _mm_clmulepi64_si128 ( a, b, 0x00 );
		t4 = _mm_clmulepi64_si128 ( a, b, 0x10 );
		t5 = _mm_clmulepi64_si128 ( a, b, 0x01 );
		t6 = _mm_clmulepi64_si128 ( a, b, 0x11 );
		t4 = _mm_xor_si128 ( t4, t5 );
		t5 = _mm_slli_si128 ( t4, 8 );
		t4 = _mm_srli_si128 ( t4, 8 );
		t3 = _mm_xor_si128 ( t3, t5 );
		t6 = _mm_xor_si128 ( t6, t4 );
		t7 = _mm_srli_epi32 ( t3, 31 );
		t8 = _mm_srli_epi32 ( t6, 31 );
		t3 = _mm_slli_epi32 ( t3, 1 );
		t6 = _mm_slli_epi32 ( t6, 1 );
		t9 = _mm_srli_si128 ( t7, 12 );
		t8 = _mm_slli_si128 ( t8, 4 );
		t7 = _mm_slli_si128 ( t7, 4 );
		t3 = _mm_or_si128 ( t3, t7 );
		t6 = _mm_or_si128 ( t6, t8 );
		t6 = _mm_or_si128 ( t6, t9 );
		t7 = _mm_slli_epi32 ( t3, 31 );
		t8 = _mm_slli_epi32 ( t3, 30 );
		t9 = _mm_slli_epi32 ( t3, 25 );
		t7 = _mm_xor_si128 ( t7, t8 );
		t7 = _mm_xor_si128 ( t7, t9 );
		t8 = _mm_srli_si128 ( t7, 4 );
		t7 = _mm_slli_si128 ( t7, 12 );
		t3 = _mm_xor_si128 ( t3, t7 );
		t2 = _mm_srli_epi32 ( t3, 1 );
		t4 = _mm_srli_epi32 ( t3, 2 );
		t5 = _mm_srli_epi32 ( t3, 7 );
		t2 = _mm_xor_si128 ( t2, t4 );
		t2 = _mm_xor_si128 ( t2, t5 );
		t2 = _mm_xor_si128 ( t2, t8 );
		t3 = _mm_xor_si128 ( t3, t2 );

		return _mm_xor_si128 ( t6, t3 );

	}

	CRYPTO_TARGET void aes_gcm_setup ( aes_gcm_key& key, const uint8_t secret [ AES_GCM_KEY_BYTES ] ) {

		__m128i* rounds = reinterpret_cast < __m128i* > ( key.rounds );
		__m128i k = _mm_loadu_si128 ( reinterpret_cast < const __m128i* > ( secret ) );

		_mm_store_si128 ( rounds, k );
		k = expand_step ( k, _mm_aeskeygenassist_si128 ( k, 0x01 ) );
		_mm_store_si128 ( rounds + 1, k );
		k = expand_step ( k, _mm_aeskeygenassist_si128 ( k, 0x02 ) );
		_mm_store_si128 ( rounds + 2, k );
		k = expand_step ( k, _mm_aeskeygenassist_si128 ( k, 0x04 ) );
		_mm_store_si128 ( rounds + 3, k );
		k = expand_step ( k, _mm_aeskeygenassist_si128 ( k, 0x08 ) );
		_mm_store_si128 ( rounds + 4, k );
		k = expand_step ( k, _mm_aeskeygenassist_si128 ( k, 0x10 ) );
		_mm_store_si128 ( rounds + 5, k );
		k = expand_step ( k, _mm_aeskeygenassist_si128 ( k, 0x20 ) );
		_mm_store_si128 ( rounds + 6, k );
		k = expand_step ( k, _mm_aeskeygenassist_si128 ( k, 0x40 ) );
		_mm_store_si128 ( rounds + 7, k );
		k = expand_step ( k, _mm_aeskeygenassist_si128 ( k, 0x80 ) );
		_mm_store_si128 ( rounds + 8, k );
		k = expand_step ( k, _mm_aeskeygenassist_si128 ( k, 0x1B ) );
		_mm_store_si128 ( rounds + 9, k );
		k = expand_step ( k, _mm_aeskeygenassist_si128 ( k, 0x36 ) );
		_mm_store_si128 ( rounds + 10, k );

		__m128i h = encrypt_block ( key, _mm_setzero_si128 ( ) );

		_mm_store_si128 ( reinterpret_cast < __m128i* > ( key.hash ), reverse ( h ) );

	}

	CRYPTO_TARGET static __m128i load_partial ( const uint8_t* data, size_t size ) {

		alignas ( 16 ) uint8_t block [ 16 ] = { };

		for ( size_t i = 0; i < size; i++ )
			block [ i ] = data [ i ];

		return _mm_load_si128 ( reinterpret_cast < const __m128i* > ( block ) );

	}

	CRYPTO_TARGET static __m128i absorb ( __m128i state, __m128i h, const uint8_t* data, size_t size ) {

		while ( size >= 16 ) {

			__m128i block = reverse ( _mm_loadu_si128 ( reinterpret_cast < const __m128i* > ( data ) ) );

			state = multiply ( _mm_xor_si128 ( state, block ), h );
			data += 16;
			size -= 16;

		}

		if ( size )
			state = multiply ( _mm_xor_si128 ( state, reverse ( load_partial ( data, size ) ) ), h );

		return state;

	}

	CRYPTO_TARGET static __m128i counter_block ( const uint8_t nonce [ 12 ], uint32_t counter ) {

		alignas ( 16 ) uint8_t block [ 16 ];

		for ( int i = 0; i < 12; i++ )
			block [ i ] = nonce [ i ];

		block [ 12 ] = static_cast < uint8_t > ( counter >> 24 );
		block [ 13 ] = static_cast < uint8_t > ( counter >> 16 );
		block [ 14 ] = static_cast < uint8_t > ( counter >> 8 );
		block [ 15 ] = static_cast < uint8_t > ( counter );

		return _mm_load_si128 ( reinterpret_cast < const __m128i* > ( block ) );

	}

	CRYPTO_TARGET static void apply_stream ( const aes_gcm_key& key, const uint8_t nonce [ 12 ], const uint8_t* in, size_t size, uint8_t* out ) {

		uint32_t counter = 2;

		while ( size >= 16 ) {

			__m128i stream = encrypt_block ( key, counter_block ( nonce, counter++ ) );
			__m128i data = _mm_loadu_si128 ( reinterpret_cast < const __m128i* > ( in ) );

			_mm_storeu_si128 ( reinterpret_cast < __m128i* > ( out ), _mm_xor_si128 ( data, stream ) );
			in += 16;
			out += 16;
			size -= 16;

		}

		if ( size ) {

			alignas ( 16 ) uint8_t stream [ 16 ];

			_mm_store_si128 ( reinterpret_cast < __m128i* > ( stream ), encrypt_block ( key, counter_block ( nonce, counter ) ) );

			for ( size_t i = 0; i < size; i++ )
				out [ i ] = in [ i ] ^ stream [ i ];

		}

	}

	CRYPTO_TARGET static void make_tag ( const aes_gcm_key& key, const uint8_t nonce [ 12 ], const uint8_t* aad, size_t aad_size, const uint8_t* sealed, size_t size, uint8_t tag [ 16 ] ) {

		__m128i h = _mm_load_si128 ( reinterpret_cast < const __m128i* > ( key.hash ) );
		__m128i state = _mm_setzero_si128 ( );

		state = absorb ( state, h, aad, aad_size );
		state = absorb ( state, h, sealed, size );

		__m128i lengths = _mm_set_epi64x ( static_cast < long long > ( aad_size ) * 8, static_cast < long long > ( size ) * 8 );

		state = multiply ( _mm_xor_si128 ( state, lengths ), h );

		__m128i mask = encrypt_block ( key, counter_block ( nonce, 1 ) );

		_mm_storeu_si128 ( reinterpret_cast < __m128i* > ( tag ), _mm_xor_si128 ( reverse ( state ), mask ) );

	}

	void aes_gcm_seal ( const aes_gcm_key& key, const uint8_t nonce [ AES_GCM_NONCE_BYTES ], const uint8_t* aad, size_t aad_size, const uint8_t* plain, size_t size, uint8_t* out ) {

		apply_stream ( key, nonce, plain, size, out );
		make_tag ( key, nonce, aad, aad_size, out, size, out + size );

	}

	bool aes_gcm_open ( const aes_gcm_key& key, const uint8_t nonce [ AES_GCM_NONCE_BYTES ], const uint8_t* aad, size_t aad_size, const uint8_t* sealed, size_t size, uint8_t* out ) {

		if ( size < AES_GCM_TAG_BYTES )
			return false;

		size_t body = size - AES_GCM_TAG_BYTES;
		uint8_t tag [ 16 ];
		uint8_t difference = 0;

		make_tag ( key, nonce, aad, aad_size, sealed, body, tag );

		for ( int i = 0; i < 16; i++ )
			difference |= tag [ i ] ^ sealed [ body + i ];

		if ( difference )
			return false;

		apply_stream ( key, nonce, sealed, body, out );

		return true;

	}

	void aes_gcm_forget ( aes_gcm_key& key ) {

		volatile uint8_t* bytes = reinterpret_cast < volatile uint8_t* > ( &key );

		for ( size_t i = 0; i < sizeof ( key ); i++ )
			bytes [ i ] = 0;

	}

}
