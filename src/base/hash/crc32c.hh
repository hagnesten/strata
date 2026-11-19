#pragma once

#include <stdint.h>
#include <stddef.h>

#define CRC_POLY 0x82F63B78u
#define CRC_LANE 256

namespace base {

	struct crc_tables {

		uint32_t slices [ 8 ] [ 256 ];
		uint32_t lane [ 4 ] [ 256 ];
		bool hardware;
		bool ready;

	};

	uint32_t crc32c ( const void* data, size_t size, uint32_t start = 0 );

	uint32_t crc32c_portable ( const void* data, size_t size, uint32_t start = 0 );

	bool crc32c_hardware ( );

}
