#pragma once

#include <stdint.h>
#include <stddef.h>

#define P256_SCALAR_BYTES 32
#define P256_POINT_BYTES 65

namespace base {

	struct p256_modulus {

		uint64_t m [ 4 ];
		uint64_t inverse;
		uint64_t one [ 4 ];
		uint64_t square [ 4 ];

	};

	struct p256_point {

		uint64_t x [ 4 ];
		uint64_t y [ 4 ];
		uint64_t z [ 4 ];

	};

	bool p256_public ( const uint8_t secret [ P256_SCALAR_BYTES ], uint8_t out [ P256_POINT_BYTES ] );

	bool p256_sign ( const uint8_t secret [ P256_SCALAR_BYTES ], const uint8_t digest [ 32 ], uint8_t r [ 32 ], uint8_t s [ 32 ] );

	bool p256_verify ( const uint8_t point [ P256_POINT_BYTES ], const uint8_t digest [ 32 ], const uint8_t r [ 32 ], const uint8_t s [ 32 ] );

	bool p256_new_secret ( uint8_t out [ P256_SCALAR_BYTES ] );

}
