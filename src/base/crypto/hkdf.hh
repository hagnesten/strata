#pragma once

#include "base/hash/sha256.hh"

namespace base {

	void hkdf_extract ( const uint8_t* salt, size_t salt_size, const uint8_t* input, size_t input_size, uint8_t out [ SHA256_BYTES ] );

	void hkdf_expand ( const uint8_t secret [ SHA256_BYTES ], const uint8_t* info, size_t info_size, uint8_t* out, size_t size );

	void hkdf_label ( const uint8_t secret [ SHA256_BYTES ], const char* label, const uint8_t* context, size_t context_size, uint8_t* out, size_t size );

}
