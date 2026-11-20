#pragma once

#include <stdint.h>
#include <stddef.h>

#define SHA256_BYTES 32
#define SHA256_BLOCK 64

namespace base {

	struct sha256_state {

		uint32_t words [ 8 ];
		uint8_t block [ SHA256_BLOCK ];
		uint32_t used;
		uint64_t total;

	};

	struct hmac_state {

		sha256_state inner;
		sha256_state outer;

	};

	void sha256_start ( sha256_state& state );

	void sha256_add ( sha256_state& state, const void* data, size_t size );

	void sha256_finish ( sha256_state& state, uint8_t out [ SHA256_BYTES ] );

	void sha256 ( const void* data, size_t size, uint8_t out [ SHA256_BYTES ] );

	void hmac_start ( hmac_state& state, const void* key, size_t size );

	void hmac_add ( hmac_state& state, const void* data, size_t size );

	void hmac_finish ( hmac_state& state, uint8_t out [ SHA256_BYTES ] );

	void hmac_sha256 ( const void* key, size_t key_size, const void* data, size_t size, uint8_t out [ SHA256_BYTES ] );

	void pbkdf2_sha256 ( const void* password, size_t password_size, const void* salt, size_t salt_size, uint32_t count, uint8_t* out, size_t size );

}
