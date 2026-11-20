#pragma once

#include <stdint.h>

namespace base {

	struct text;

}

namespace app {

	bool check_file ( const char* path, base::text& summary, bool& clean );

	int32_t run_check ( const char* path );

	int32_t run_backup ( const char* source, const char* target );

	int32_t run_restore ( const char* backup, const char* target );

}
