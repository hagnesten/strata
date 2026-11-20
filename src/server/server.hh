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
#define SERVER_BUSY_LIMIT 120000
#define SERVER_LOCK_RETRY 5
#define SERVER_CHANNELS 64
#define SERVER_OBJECT_ROOM 160
#define SERVER_GRANTS 4096
#define SERVER_ACCESS_NAMES 256
#define SERVER_QUERY_ROOM 1024
#define ACCESS_SELECT 1
#define ACCESS_INSERT 2
#define ACCESS_UPDATE 4
#define ACCESS_DELETE 8
#define ACCESS_CREATE 16
#define ACCESS_ALL 31
#define SERVER_NOTICE_ROOM 8000

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
#define PG_VARCHAR 1043
#define PG_BPCHAR 1042
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
#define PG_INTERVAL 1186
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
		bool limited;

	};

	struct access_list {

		const char* names [ SERVER_ACCESS_NAMES ];
		uint32_t privileges [ SERVER_ACCESS_NAMES ];
		uint32_t count;
		const char* ctes [ SERVER_ACCESS_NAMES ];
		uint32_t cte_count;
		bool overflow;

	};

	struct server_grant {

		char grantee [ SERVER_NAME_ROOM ];
		char object [ SERVER_OBJECT_ROOM ];
		uint32_t privileges;

	};

	struct server_settings {

		const char* path;
		const char* host;
		uint16_t port;
		bool trust;
		bool quiet;
		const char* backup_folder;
		uint32_t backup_minutes;
		uint32_t backup_keep;
		uint32_t slow_milliseconds;
		const char* log_path;
		bool tls;
		bool tls_required;
		const char* tls_certificate;
		const char* tls_key;

	};

	struct server_statement {

		char name [ SERVER_NAME_ROOM ];
		strata_stmt* stmt;
		uint32_t parameter_count;
		uint32_t parameter_types [ SERVER_PARAMETERS ];
		uint32_t column_count;
		uint32_t column_types [ SERVER_COLUMNS ];
		int32_t column_modifiers [ SERVER_COLUMNS ];
		bool returns_rows;
		char tag [ 32 ];
		bool control;
		char control_text [ 256 ];
		char* body;
		size_t body_size;
		char text [ SERVER_QUERY_ROOM ];

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
		bool lock_wait;
		uint64_t busy_since;
		uint64_t ticket;
		strata_db* db;
		char user [ SERVER_NAME_ROOM ];
		bool limited;
		char application [ SERVER_NAME_ROOM ];
		char started [ 40 ];
		char query [ SERVER_QUERY_ROOM ];
		char query_start [ 40 ];
		bool active;
		bool terminate;
		char database [ SERVER_NAME_ROOM ];
		server_statement* statements [ SERVER_STATEMENTS ];
		uint32_t statement_count;
		server_portal portal;
		bool skipping;
		bool failed;
		bool rolled_back;
		char channels [ SERVER_CHANNELS ] [ SERVER_NAME_ROOM ];
		uint32_t channel_count;
		base::text notices;
		uint32_t secret;
		uint32_t id;
		struct tls_session* tls;

	};

	struct server_state {

		server_settings settings;
		void* listener;
		server_connection* connections [ SERVER_CONNECTIONS ];
		uint32_t connection_count;
		server_user users [ SERVER_USERS ];
		server_grant* grants;
		uint32_t grant_count;
		uint32_t user_count;
		uint32_t next_id;
		uint64_t next_ticket;
		uint64_t last_retry;
		uint64_t next_backup;
		void* backup_process;
		char backup_target [ 1024 ];
		uint64_t started_ms;
		bool released;
		bool running;
		struct tls_identity* identity;

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

	int32_t manage_user ( const char* path, const char* name, const char* password, bool limited );

	bool save_users ( const char* path, const server_user* users, uint32_t count );

	bool load_grants ( server_state& state );

	bool save_grants ( server_state& state );

	bool access_allowed ( server_connection& link, const sql::statement* statement, base::text& problem );

	void grant_owner ( server_connection& link, const char* name );

	void time_stamp ( base::text& out, bool compact );

	void server_log ( server_state& state, const char* line );

	void tend_backups ( server_state& state, uint64_t now );

	void watch_activity ( server_state& state );

	void begin_activity ( server_connection& link, const char* text );

	void end_activity ( server_connection& link, uint64_t started );

	bool access_command ( server_connection& link, const char* tag, const char* text, size_t size, base::text& problem, const char*& code );

	bool load_users ( server_state& state );

	bool find_user ( server_state& state, const char* name, server_user*& out );

	bool make_user ( const char* name, const char* password, server_user& out );

	void serve_connection ( void* context );

	bool need_bytes ( server_connection& link, size_t count );

	bool read_message ( server_connection& link, pg_message& out );

	bool flush_output ( server_connection& link );

	bool reserve_input ( server_connection& link, size_t extra );

	bool output_waiting ( const server_connection& link );

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

	int32_t column_modifier ( const engine::prepared_select* plan, uint32_t column );

	uint32_t declared_type ( const char* type );

	bool encode_value ( server_connection& link, const storage::value& item, uint32_t type, bool binary );

	bool decode_parameter ( base::arena& memory, const uint8_t* data, int32_t size, uint32_t type, bool binary, strata_value& out );

	const char* error_code ( const char* message, bool parse_failed );

	bool control_statement ( const char* sql, size_t size, char* tag, size_t tag_room, char* show, size_t show_room, bool& handled );

	bool wait_busy ( server_connection& link );

	bool dollar_body ( const char* sql, size_t size, size_t& body, size_t& body_size, size_t& after );

	bool run_do_block ( server_connection& link, const char* body, size_t size, base::text& problem );

	bool listen_command ( server_connection& link, const char* tag, const char* text, size_t size, base::text& problem );

	void deliver_notices ( server_connection& link, bool committed );

}
