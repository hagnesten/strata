#pragma once

#include "storage/record/record.hh"

namespace engine {

	enum moment_kind : uint8_t {

		MOMENT_NONE,
		MOMENT_DATE,
		MOMENT_TIMESTAMP,
		MOMENT_ZONED,
		MOMENT_TIME,

	};

	struct moment_parts {

		int64_t days;
		int64_t micros;
		bool has_date;
		bool has_time;

	};

	moment_kind moment_type ( const char* type );

	bool parse_moment ( const uint8_t* text, size_t size, bool zoned, moment_parts& out );

	void write_moment ( const moment_parts& parts, moment_kind kind, base::text& out );

	bool canonical_moment ( const uint8_t* text, size_t size, moment_kind kind, base::text& out );

	int32_t moment_infinity ( const uint8_t* text, size_t size );

	bool moment_field ( const char* field, size_t field_size, const moment_parts& parts, double& out );

	bool truncate_moment ( const char* field, size_t field_size, moment_parts& parts );

	int64_t moment_days_from_civil ( int64_t year, int64_t month, int64_t day );

	void moment_civil_from_days ( int64_t days, int64_t& year, int64_t& month, int64_t& day );

}
