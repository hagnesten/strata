#include "base/clock/clock.hh"

#include <windows.h>

namespace base {

	int64_t unix_milliseconds ( ) {

		FILETIME now;

		GetSystemTimeAsFileTime ( &now );

		uint64_t ticks = static_cast < uint64_t > ( now.dwHighDateTime ) << 32 | now.dwLowDateTime;

		return static_cast < int64_t > ( ticks / 10000 ) - 11644473600000ll;

	}

	int64_t local_offset_milliseconds ( ) {

		TIME_ZONE_INFORMATION zone;
		DWORD mode = GetTimeZoneInformation ( &zone );
		int64_t bias = zone.Bias;

		if ( mode == TIME_ZONE_ID_DAYLIGHT )
			bias += zone.DaylightBias;
		else if ( mode == TIME_ZONE_ID_STANDARD )
			bias += zone.StandardBias;

		return -bias * 60000;

	}

	uint64_t precise_ticks ( ) {

		LARGE_INTEGER now;

		QueryPerformanceCounter ( &now );

		return static_cast < uint64_t > ( now.QuadPart );

	}

	uint64_t tick_rate ( ) {

		LARGE_INTEGER rate;

		QueryPerformanceFrequency ( &rate );

		return static_cast < uint64_t > ( rate.QuadPart );

	}

}
