#pragma once

#include "base/text/text.hh"
#include "base/bytes/bytes.hh"

#define RECORD_COLUMNS 256
#define PROBE_FIELDS 16

namespace storage {

	enum value_kind : uint8_t {

		VALUE_NULL,
		VALUE_INTEGER,
		VALUE_REAL,
		VALUE_TEXT,
		VALUE_BLOB,
		VALUE_DECIMAL,

	};

	struct value {

		value_kind kind;
		uint8_t subtype;
		int64_t integer;
		double real;
		const uint8_t* bytes;
		size_t size;

	};

	struct key_probe {

		const uint8_t* key;
		size_t size;
		uint32_t columns;
		uint32_t fields;
		uint64_t count;
		bool decoded;
		value values [ PROBE_FIELDS ];

	};

	inline value null_value ( ) {

		value made = { };

		made.kind = VALUE_NULL;

		return made;

	}

	inline value integer_value ( int64_t number ) {

		value made = { };

		made.kind = VALUE_INTEGER;
		made.integer = number;

		return made;

	}

	inline value real_value ( double number ) {

		value made = { };

		made.kind = VALUE_REAL;
		made.real = number;

		return made;

	}

	inline value text_value ( const char* data, size_t size ) {

		value made = { };

		made.kind = VALUE_TEXT;
		made.bytes = reinterpret_cast < const uint8_t* > ( data );
		made.size = size;

		return made;

	}

	inline value blob_value ( const uint8_t* data, size_t size ) {

		value made = { };

		made.kind = VALUE_BLOB;
		made.bytes = data;
		made.size = size;

		return made;

	}

	inline value decimal_raw ( __int128 mantissa, uint8_t scale ) {

		value made = { };
		unsigned __int128 bits = static_cast < unsigned __int128 > ( mantissa );

		made.kind = VALUE_DECIMAL;
		made.subtype = scale;
		made.integer = static_cast < int64_t > ( static_cast < uint64_t > ( bits ) );
		made.size = static_cast < size_t > ( static_cast < uint64_t > ( bits >> 64 ) );

		return made;

	}

	inline __int128 decimal_mantissa ( const value& item ) {

		unsigned __int128 bits = static_cast < unsigned __int128 > ( static_cast < uint64_t > ( item.size ) ) << 64 | static_cast < uint64_t > ( item.integer );

		return static_cast < __int128 > ( bits );

	}

	void write_decimal ( const value& item, base::text& out );

	size_t record_size ( const value* values, uint32_t count );

	void write_record ( const value* values, uint32_t count, uint8_t* out );

	void encode_record ( const value* values, uint32_t count, base::text& out );

	bool decode_record ( const uint8_t* data, size_t size, value* out, uint32_t room, uint32_t& count );

	bool decode_wanted ( const uint8_t* data, size_t size, value* out, uint32_t room, const bool* wanted, uint32_t limit, uint32_t& count );

	int32_t compare_values ( const value& a, const value& b );

	int32_t compare_records ( const uint8_t* a, size_t a_size, const uint8_t* b, size_t b_size, uint32_t columns );

	bool field_at ( const uint8_t* data, size_t size, uint32_t column, value& out, bool& present );

	bool last_integer ( const uint8_t* data, size_t size, int64_t& out );

	void make_probe ( const uint8_t* key, size_t size, uint32_t columns, key_probe& out );

	int32_t compare_probe ( const uint8_t* data, size_t size, const key_probe& probe );

}
