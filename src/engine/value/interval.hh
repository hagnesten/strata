#pragma once

#include "engine/value/moment.hh"

namespace engine {

	struct interval_parts {

		int64_t months;
		int64_t days;
		int64_t micros;

	};

	bool parse_interval ( const uint8_t* text, size_t size, interval_parts& out );

	void write_interval ( const interval_parts& parts, base::text& out );

	void shift_moment ( moment_parts& moment, const interval_parts& by, int64_t sign );

	interval_parts moment_gap ( const moment_parts& later, const moment_parts& earlier );

	int64_t interval_key ( const interval_parts& parts );

	double interval_epoch ( const interval_parts& parts );

	bool interval_field ( const char* field, size_t size, const interval_parts& parts, double& out );

	interval_parts scale_interval ( const interval_parts& parts, double factor );

}
