#pragma once

#include <stdint.h>
#include <stddef.h>

#define X25519_BYTES 32

namespace base {

	struct x25519_field {

		uint64_t limb [ 5 ];

	};

	bool x25519 ( const uint8_t scalar [ X25519_BYTES ], const uint8_t point [ X25519_BYTES ], uint8_t out [ X25519_BYTES ] );

	void x25519_public ( const uint8_t scalar [ X25519_BYTES ], uint8_t out [ X25519_BYTES ] );

}
