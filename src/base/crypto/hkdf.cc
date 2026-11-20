#include "base/crypto/hkdf.hh"
#include "base/text/text.hh"

namespace base {

	void hkdf_extract ( const uint8_t* salt, size_t salt_size, const uint8_t* input, size_t input_size, uint8_t out [ SHA256_BYTES ] ) {

		uint8_t zero [ SHA256_BYTES ] = { };

		hmac_sha256 ( salt_size ? salt : zero, salt_size ? salt_size : SHA256_BYTES, input, input_size, out );

	}

	void hkdf_expand ( const uint8_t secret [ SHA256_BYTES ], const uint8_t* info, size_t info_size, uint8_t* out, size_t size ) {

		uint8_t block [ SHA256_BYTES ];
		size_t done = 0;
		uint8_t counter = 1;

		while ( done < size ) {

			hmac_state mac;

			hmac_start ( mac, secret, SHA256_BYTES );

			if ( counter > 1 )
				hmac_add ( mac, block, SHA256_BYTES );

			hmac_add ( mac, info, info_size );
			hmac_add ( mac, &counter, 1 );
			hmac_finish ( mac, block );

			size_t take = size - done < SHA256_BYTES ? size - done : SHA256_BYTES;

			for ( size_t i = 0; i < take; i++ )
				out [ done + i ] = block [ i ];

			done += take;
			counter++;

		}

		for ( size_t i = 0; i < SHA256_BYTES; i++ )
			block [ i ] = 0;

	}

	void hkdf_label ( const uint8_t secret [ SHA256_BYTES ], const char* label, const uint8_t* context, size_t context_size, uint8_t* out, size_t size ) {

		uint8_t info [ 2 + 1 + 255 + 1 + 255 ];
		size_t label_size = length ( label );
		size_t at = 0;

		info [ at++ ] = static_cast < uint8_t > ( size >> 8 );
		info [ at++ ] = static_cast < uint8_t > ( size );
		info [ at++ ] = static_cast < uint8_t > ( 6 + label_size );

		for ( const char* prefix = "tls13 "; *prefix; prefix++ )
			info [ at++ ] = static_cast < uint8_t > ( *prefix );

		for ( size_t i = 0; i < label_size; i++ )
			info [ at++ ] = static_cast < uint8_t > ( label [ i ] );

		info [ at++ ] = static_cast < uint8_t > ( context_size );

		for ( size_t i = 0; i < context_size; i++ )
			info [ at++ ] = context [ i ];

		hkdf_expand ( secret, info, at, out, size );

	}

}
