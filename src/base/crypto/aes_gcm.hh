#pragma once

#include <stdint.h>
#include <stddef.h>

#define AES_GCM_KEY_BYTES 16
#define AES_GCM_NONCE_BYTES 12
#define AES_GCM_TAG_BYTES 16

namespace base {

	struct aes_gcm_key {

		alignas ( 16 ) uint8_t rounds [ 11 * 16 ];
		alignas ( 16 ) uint8_t hash [ 16 ];

	};

	bool aes_gcm_supported ( );

	void aes_gcm_setup ( aes_gcm_key& key, const uint8_t secret [ AES_GCM_KEY_BYTES ] );

	void aes_gcm_seal ( const aes_gcm_key& key, const uint8_t nonce [ AES_GCM_NONCE_BYTES ], const uint8_t* aad, size_t aad_size, const uint8_t* plain, size_t size, uint8_t* out );

	bool aes_gcm_open ( const aes_gcm_key& key, const uint8_t nonce [ AES_GCM_NONCE_BYTES ], const uint8_t* aad, size_t aad_size, const uint8_t* sealed, size_t size, uint8_t* out );

	void aes_gcm_forget ( aes_gcm_key& key );

}
