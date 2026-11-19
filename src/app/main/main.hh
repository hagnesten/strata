#pragma once

#include <stdint.h>

#define STRATA_VERSION "0.6"
#define MAIN_ARGUMENTS 16

namespace app {

	int32_t run ( int32_t argument_count, char** arguments, bool windowed );

}
