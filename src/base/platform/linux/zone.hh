#pragma once

#include <stdint.h>
#include <stddef.h>

namespace base {

	int64_t zone_offset_seconds ( const uint8_t* data, size_t size, int64_t now );

}
