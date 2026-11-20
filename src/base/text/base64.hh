#pragma once

#include "base/text/text.hh"

namespace base {

	void add_base64 ( text& out, const void* data, size_t size );

	bool read_base64 ( const char* value, size_t size, uint8_t* out, size_t room, size_t& got );

}
