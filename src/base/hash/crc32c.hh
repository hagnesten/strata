#pragma once

#include <stdint.h>
#include <stddef.h>

namespace base {

	uint32_t crc32c ( const void* data, size_t size, uint32_t start = 0 );

}
