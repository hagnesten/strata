#pragma once

#include "base/text/text.hh"

namespace base {

	void write ( const char* value );

	void write ( const text& value );

	void write_error ( const char* value );

	bool read_line ( text& out );

	void use_utf8 ( );

}
