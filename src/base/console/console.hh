#pragma once

#include <stdint.h>
#include <stddef.h>

namespace base {

	size_t length ( const char* text );

	void write ( const char* text );

	void write_error ( const char* text );

}
