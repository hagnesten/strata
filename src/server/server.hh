#pragma once

#include "api/api.hh"
#include "base/platform/os.hh"
#include "base/hash/sha256.hh"

#define SERVER_CONNECTIONS 500
#define SERVER_INPUT 65536
#define SERVER_OUTPUT_LIMIT ( 4u * 1024u * 1024u )
#define SERVER_NAME_ROOM 64
#define SERVER_USERS 256
#define SERVER_STATEMENTS 256
#define SERVER_COLUMNS 256
#define SERVER_PARAMETERS 1024
#define SERVER_ITERATIONS 4096
#define SERVER_SALT 16
#define SERVER_BUSY_LIMIT 10000

#define PG_PROTOCOL 196608
#define PG_SSL_REQUEST 80877103
#define PG_GSS_REQUEST 80877104
#define PG_CANCEL_REQUEST 80877102

#define PG_BOOL 16
#define PG_BYTEA 17
#define PG_INT8 20
#define PG_INT2 21
#define PG_INT4 23
#define PG_TEXT 25
#define PG_OID 26
#define PG_JSON 114
#define PG_FLOAT4 700
#define PG_FLOAT8 701
#define PG_UNKNOWN 705
#define PG_VARCHAR 1043
#define PG_DATE 1082
#define PG_TIME 1083
#define PG_TIMESTAMP 1114
#define PG_TIMESTAMPTZ 1184
#define PG_NUMERIC 1700
#define PG_UUID 2950
#define PG_JSONB 3802

namespace server {

	struct server_user {

		char name [ SERVER_NAME_ROOM ];
		uint8_t salt [ SERVER_SALT ];
		uint32_t iterations;
		uint8_t stored [ SHA256_BYTES ];
		uint8_t check [ SHA256_BYTES ];

	};

	struct server_settings {

		const char* path;
		const char* host;
		uint16_t port;
		bool trust;
		bool quiet;

	};

	struct server_statement {

		char name [ SERVER_NAME_ROOM ];
		strata_stmt* stmt;
		uint32_t parameter_count;
		uint32_t parameter_types [ SERVER_PARAMETERS ];
		uint32_t column_count;
		uint32_t column_types [ SERVER_COLUMNS ];
		bool returns_rows;
		char tag [ 32 ];
		bool control;
		char control_text [ 256 ];

	};

	struct server_portal {

		char name [ SERVER_NAME_ROOM ];
		server_statement* statement;
		int16_t formats [ SERVER_COLUMNS ];
		uint32_t format_count;
		bool started;
		bool finished;
		uint64_t rows;

	};

	struct server_state;

	struct server_connection {

		server_state* owner;
		void* socket;
		base::coroutine* routine;
		uint8_t* input;
		size_t input_room;
		size_t input_start;
		size_t input_end;
		base::text output;
		size_t output_sent;
		size_t message_start;
		bool reading;
		bool closed;
		bool finished;
		bool busy_wait;
		uint64_t busy_since;
		strata_db* db;
		char user [ SERVER_NAME_ROOM ];
		char database [ SERVER_NAME_ROOM ];
		server_statement* statements [ SERVER_STATEMENTS ];
		uint32_t statement_count;
		server_portal portal;
		bool skipping;
		uint32_t secret;
		uint32_t id;

	};

	struct server_state {

		server_settings settings;
		void* listener;
		server_connection* connections [ SERVER_CONNECTIONS ];
		uint32_t connection_count;
		server_user users [ SERVER_USERS ];
		uint32_t user_count;
		uint32_t next_id;
		bool running;

	};

	struct pg_message {

		uint8_t type;
		const uint8_t* body;
		uint32_t size;

	};

	struct pg_reader {

		const uint8_t* at;
		const uint8_t* end;
		bool failed;

	};

	int32_t run_server ( const server_settings& settings );

	int32_t manage_user ( const char* path, const char* name, const char* password );

	bool load_users ( server_state& state );

	bool find_user ( server_state& state, const char* name, server_user*& out );

	bool make_user ( const char* name, const char* password, server_user& out );

	void serve_connection ( void* context );

	bool need_bytes ( server_connection& link, size_t count );

	bool read_message ( server_connection& link, pg_message& out );

	bool flush_output ( server_connection& link );

	void begin_message ( server_connection& link, uint8_t type );

	void put16 ( server_connection& link, int32_t value );

	void put32 ( server_connection& link, int32_t value );

	void put_bytes ( server_connection& link, const void* data, size_t size );

	void put_string ( server_connection& link, const char* value );

	void end_message ( server_connection& link );

	void send_error ( server_connection& link, const char* code, const char* message );

	void send_ready ( server_connection& link );

	bool sign_in ( server_connection& link );

	bool handle_simple ( server_connection& link, const pg_message& message );

	bool handle_extended ( server_connection& link, const pg_message& message );

	void forget_statements ( server_connection& link );

	uint8_t read8 ( pg_reader& reader );

	int16_t read16 ( pg_reader& reader );

	int32_t read32 ( pg_reader& reader );

	const char* read_string ( pg_reader& reader );

	const uint8_t* read_span ( pg_reader& reader, size_t size );

	uint32_t column_type ( const engine::prepared_select* plan, uint32_t column );

	uint32_t declared_type ( const char* type );

	bool encode_value ( server_connection& link, const storage::value& item, uint32_t type, bool binary );

	bool decode_parameter ( base::arena& memory, const uint8_t* data, int32_t size, uint32_t type, bool binary, strata_value& out );

	const char* error_code ( const char* message, bool parse_failed );

	bool control_statement ( const char* sql, size_t size, char* tag, size_t tag_room, char* show, size_t show_room, bool& handled );

	bool wait_busy ( server_connection& link );

}
