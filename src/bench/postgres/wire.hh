#pragma once

#include "base/text/text.hh"
#include "base/hash/sha256.hh"

#define WIRE_PROTOCOL 196608
#define WIRE_COLUMNS 64
#define WIRE_PARAMETERS 32
#define WIRE_FIELD_ROOM 256
#define WIRE_INPUT_ROOM 65536
#define WIRE_OUTPUT_ROOM 65536
#define WIRE_DEFAULT_PORT 5432

#define WIRE_BOOL 16
#define WIRE_INT8 20
#define WIRE_INT2 21
#define WIRE_INT4 23
#define WIRE_TEXT 25
#define WIRE_OID 26
#define WIRE_FLOAT4 700
#define WIRE_FLOAT8 701
#define WIRE_VARCHAR 1043
#define WIRE_NUMERIC 1700

#define WIRE_AUTH_OK 0
#define WIRE_AUTH_CLEARTEXT 3
#define WIRE_AUTH_MD5 5
#define WIRE_AUTH_SASL 10
#define WIRE_AUTH_SASL_CONTINUE 11
#define WIRE_AUTH_SASL_FINAL 12

namespace bench {

	struct wire_settings {

		char host [ WIRE_FIELD_ROOM ];
		char user [ WIRE_FIELD_ROOM ];
		char password [ WIRE_FIELD_ROOM ];
		char database [ WIRE_FIELD_ROOM ];
		uint16_t port;

	};

	struct wire_message {

		uint8_t type;
		const uint8_t* body;
		uint32_t size;

	};

	struct wire_link {

		void* socket;
		uint8_t* input;
		size_t input_room;
		size_t input_start;
		size_t input_end;
		uint8_t* output;
		size_t output_room;
		size_t output_size;
		size_t message_start;
		bool output_failed;
		bool broken;
		uint8_t status;
		char server_version [ 64 ];
		base::text problem;

	};

	struct sign_in_state {

		base::text nonce;
		base::text client_first;
		base::text client_final;
		uint8_t server [ SHA256_BYTES ];
		bool proved;

	};

	struct wire_statement {

		char name [ 24 ];
		uint32_t parameter_count;
		uint32_t parameter_types [ WIRE_PARAMETERS ];
		uint32_t column_count;
		uint32_t column_types [ WIRE_COLUMNS ];
		int16_t column_formats [ WIRE_COLUMNS ];

	};

	bool read_settings ( const char* value, wire_settings& out );

	bool wire_open ( wire_link& link, const wire_settings& settings );

	void wire_close ( wire_link& link );

	void wire_begin ( wire_link& link, uint8_t type );

	void wire_int16 ( wire_link& link, int32_t value );

	void wire_int32 ( wire_link& link, int32_t value );

	void wire_bytes ( wire_link& link, const void* data, size_t size );

	void wire_string ( wire_link& link, const char* value );

	void wire_end ( wire_link& link );

	bool wire_flush ( wire_link& link );

	bool wire_next ( wire_link& link, wire_message& out );

	void wire_note_error ( wire_link& link, const wire_message& message );

	void wire_fail ( wire_link& link, const char* what );

	bool wire_settle ( wire_link& link );

	bool wire_simple ( wire_link& link, const char* sql );

	inline uint32_t wire_read32 ( const uint8_t* at ) {

		return static_cast < uint32_t > ( at [ 0 ] ) << 24 | static_cast < uint32_t > ( at [ 1 ] ) << 16 | static_cast < uint32_t > ( at [ 2 ] ) << 8 | at [ 3 ];

	}

	inline uint16_t wire_read16 ( const uint8_t* at ) {

		return static_cast < uint16_t > ( at [ 0 ] << 8 | at [ 1 ] );

	}

	inline uint64_t wire_read64 ( const uint8_t* at ) {

		return static_cast < uint64_t > ( wire_read32 ( at ) ) << 32 | wire_read32 ( at + 4 );

	}

}
