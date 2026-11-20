#pragma once

#include "base/crypto/aes_gcm.hh"
#include "base/crypto/p256.hh"
#include "base/hash/sha256.hh"
#include "base/text/text.hh"

#define TLS_RECORD_LIMIT 16384
#define TLS_CIPHER_LIMIT ( TLS_RECORD_LIMIT + 256 )
#define TLS_HEADER 5
#define TLS_RAW_ROOM ( TLS_CIPHER_LIMIT + TLS_HEADER + 64 )
#define TLS_HANDSHAKE_LIMIT 65536

#define TLS_CHANGE_CIPHER 20
#define TLS_ALERT 21
#define TLS_HANDSHAKE 22
#define TLS_APPLICATION 23

namespace server {

	struct server_connection;
	struct server_state;

	struct tls_traffic {

		base::aes_gcm_key key;
		uint8_t iv [ 12 ];
		uint8_t secret [ SHA256_BYTES ];
		uint64_t sequence;

	};

	struct tls_session {

		bool active;
		bool ended;
		tls_traffic reading;
		tls_traffic writing;
		uint8_t raw [ TLS_RAW_ROOM ];
		size_t raw_size;
		base::text sealed;
		size_t sealed_sent;
		base::text pending_handshake;
		bool update_owed;

	};

	struct tls_identity {

		uint8_t secret [ P256_SCALAR_BYTES ];
		uint8_t public_key [ P256_POINT_BYTES ];
		base::text chain;
		uint32_t chain_count;
		bool ready;

	};

	struct tls_reader {

		const uint8_t* at;
		const uint8_t* end;
		bool bad;

	};

	struct client_hello {

		uint8_t session [ 32 ];
		uint32_t session_size;
		uint8_t share [ 32 ];
		bool has_share;
		bool modern;
		bool cipher;
		bool signature;
		bool alpn_offered;
		bool alpn_postgres;

	};

	struct der_item {

		uint8_t tag;
		const uint8_t* body;
		size_t size;
		const uint8_t* next;

	};

	bool tls_prepare ( server_state& state );

	bool tls_accept ( server_connection& link );

	void tls_receive ( server_connection& link );

	bool tls_seal_output ( server_connection& link );

	bool tls_take_leftover ( server_connection& link );

	bool tls_waiting ( const server_connection& link );

	void tls_release ( server_connection& link );

	bool tls_identity_load ( tls_identity& out, const char* certificate, const char* key, base::text& problem );

	bool tls_identity_make ( tls_identity& out, const char* certificate, const char* key, base::text& problem );

	bool der_read ( const uint8_t* at, const uint8_t* end, der_item& out );

	void tls_keys ( tls_traffic& traffic, const uint8_t secret [ SHA256_BYTES ] );

	void tls_seal_record ( tls_traffic& traffic, uint8_t type, const uint8_t* data, size_t size, base::text& out );

	bool tls_open_record ( tls_traffic& traffic, const uint8_t* header, const uint8_t* body, size_t size, uint8_t* plain, size_t& plain_size, uint8_t& type );

}
