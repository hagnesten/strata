#pragma once

#include <stdint.h>

#include "engine/version.hh"

#define MAIN_ARGUMENTS 16

namespace app {

	int32_t run ( int32_t argument_count, char** arguments, bool windowed );

}
