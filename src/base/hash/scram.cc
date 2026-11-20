#include "base/hash/scram.hh"
#include "base/text/base64.hh"

namespace base {

	bool scram_field ( const char* message, size_t size, char name, const char*& value, size_t& value_size ) {

		size_t start = 0;

		while ( start < size ) {

			size_t end = start;

			while ( end < size && message [ end ] != ',' )
				end++;

			if ( end - start >= 2 && message [ start ] == name && message [ start + 1 ] == '=' ) {

				value = message + start + 2;
				value_size = end - start - 2;

				return true;

			}

			start = end + 1;

		}

		return false;

	}

	bool scram_prove ( const char* password, size_t password_size, const char* client_first, size_t client_first_size, const char* server_first, size_t server_first_size, const char* client_final, size_t client_final_size, scram_answer& out ) {

		const char* salt_text = nullptr;
		const char* count_text = nullptr;
		size_t salt_size = 0;
		size_t count_size = 0;

		if ( !scram_field ( server_first, server_first_size, 's', salt_text, salt_size ) || !scram_field ( server_first, server_first_size, 'i', count_text, count_size ) )
			return false;

		uint8_t salt [ SCRAM_SALT_ROOM ];
		size_t salt_bytes = 0;
		uint64_t count = 0;

		if ( !read_base64 ( salt_text, salt_size, salt, sizeof ( salt ), salt_bytes ) || !count_size || count_size > 9 )
			return false;

		for ( size_t i = 0; i < count_size; i++ ) {

			if ( count_text [ i ] < '0' || count_text [ i ] > '9' )
				return false;

			count = count * 10 + static_cast < uint64_t > ( count_text [ i ] - '0' );

		}

		if ( !count )
			return false;

		uint8_t salted [ SHA256_BYTES ];
		uint8_t client_key [ SHA256_BYTES ];
		uint8_t stored_key [ SHA256_BYTES ];
		uint8_t server_key [ SHA256_BYTES ];
		uint8_t signature [ SHA256_BYTES ];

		pbkdf2_sha256 ( password, password_size, salt, salt_bytes, static_cast < uint32_t > ( count ), salted, SHA256_BYTES );
		hmac_sha256 ( salted, SHA256_BYTES, "Client Key", 10, client_key );
		hmac_sha256 ( salted, SHA256_BYTES, "Server Key", 10, server_key );
		sha256 ( client_key, SHA256_BYTES, stored_key );

		hmac_state message;

		hmac_start ( message, stored_key, SHA256_BYTES );
		hmac_add ( message, client_first, client_first_size );
		hmac_add ( message, ",", 1 );
		hmac_add ( message, server_first, server_first_size );
		hmac_add ( message, ",", 1 );
		hmac_add ( message, client_final, client_final_size );
		hmac_finish ( message, signature );

		for ( uint32_t i = 0; i < SHA256_BYTES; i++ )
			out.proof [ i ] = client_key [ i ] ^ signature [ i ];

		hmac_start ( message, server_key, SHA256_BYTES );
		hmac_add ( message, client_first, client_first_size );
		hmac_add ( message, ",", 1 );
		hmac_add ( message, server_first, server_first_size );
		hmac_add ( message, ",", 1 );
		hmac_add ( message, client_final, client_final_size );
		hmac_finish ( message, out.server );

		return true;

	}

}
