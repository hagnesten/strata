#pragma once

#include <stdint.h>
#include <stddef.h>

namespace base {

	size_t utf8_room ( size_t units );

	size_t utf8_from_utf16 ( const uint16_t* units, size_t count, char* out );

	size_t utf16_from_utf8 ( const uint8_t* bytes, size_t size, uint16_t* out );

}
