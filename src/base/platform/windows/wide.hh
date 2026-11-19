#pragma once

#include <stddef.h>

namespace base {

	namespace os {

		bool wide_path ( const char* path, wchar_t* out, size_t room );

	}

}
