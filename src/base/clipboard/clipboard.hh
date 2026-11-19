#pragma once

#include "base/text/text.hh"

namespace base {

	bool copy_to_clipboard ( const char* value, size_t size );

	bool paste_from_clipboard ( text& out );

}
