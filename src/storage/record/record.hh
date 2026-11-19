#pragma once

#include "base/text/text.hh"
#include "base/bytes/bytes.hh"

#define RECORD_COLUMNS 256

namespace storage {

	enum value_kind : uint8_t {

		VALUE_NULL,
		VALUE_INTEGER,
		VALUE_REAL,
		VALUE_TEXT,
		VALUE_BLOB,

	};

	struct value {

		value_kind kind;
		int64_t integer;
		double real;
		const uint8_t* bytes;
		size_t size;

	};

	value null_value ( );

	value integer_value ( int64_t number );

	value real_value ( double number );

	value text_value ( const char* data, size_t size );

	value blob_value ( const uint8_t* data, size_t size );

	void encode_record ( const value* values, uint32_t count, base::text& out );

	bool decode_record ( const uint8_t* data, size_t size, value* out, uint32_t room, uint32_t& count );

	int32_t compare_values ( const value& a, const value& b );

	int32_t compare_records ( const uint8_t* a, size_t a_size, const uint8_t* b, size_t b_size, uint32_t columns );

}
