#include "base/clock/clock.hh"
#include "base/platform/os.hh"

namespace base {

	int64_t unix_milliseconds ( ) {

		return os::unix_milliseconds ( );

	}

	int64_t local_offset_milliseconds ( ) {

		return os::local_offset_milliseconds ( );

	}

	uint64_t precise_ticks ( ) {

		return os::ticks ( );

	}

	uint64_t tick_rate ( ) {

		return os::tick_rate ( );

	}

}
