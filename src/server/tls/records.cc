#include "server/server.hh"
#include "server/tls/tls.hh"
#include "base/crypto/hkdf.hh"
#include "base/platform/os.hh"

namespace server {

	void tls_keys ( tls_traffic& traffic, const uint8_t secret [ SHA256_BYTES ] ) {

		uint8_t key [ AES_GCM_KEY_BYTES ];

		for ( size_t i = 0; i < SHA256_BYTES; i++ )
			traffic.secret [ i ] = secret [ i ];

		base::hkdf_label ( secret, "key", nullptr, 0, key, sizeof ( key ) );
		base::hkdf_label ( secret, "iv", nullptr, 0, traffic.iv, sizeof ( traffic.iv ) );
		base::aes_gcm_setup ( traffic.key, key );
		traffic.sequence = 0;

		for ( size_t i = 0; i < sizeof ( key ); i++ )
			key [ i ] = 0;

	}

	static void record_nonce ( const tls_traffic& traffic, uint8_t out [ 12 ] ) {

		for ( int i = 0; i < 12; i++ )
			out [ i ] = traffic.iv [ i ];

		for ( int i = 0; i < 8; i++ )
			out [ 4 + i ] ^= static_cast < uint8_t > ( traffic.sequence >> ( 56 - 8 * i ) );

	}

	void tls_seal_record ( tls_traffic& traffic, uint8_t type, const uint8_t* data, size_t size, base::text& out ) {

		do {

			size_t chunk = size > TLS_RECORD_LIMIT ? TLS_RECORD_LIMIT : size;
			size_t inner = chunk + 1;
			size_t sealed = inner + AES_GCM_TAG_BYTES;
			uint8_t header [ TLS_HEADER ] = { TLS_APPLICATION, 3, 3, static_cast < uint8_t > ( sealed >> 8 ), static_cast < uint8_t > ( sealed ) };
			uint8_t nonce [ 12 ];
			size_t start = out.size;

			if ( !out.reserve ( out.size + TLS_HEADER + sealed ) )
				return;

			out.add ( reinterpret_cast < const char* > ( header ), TLS_HEADER );

			size_t body = out.size;

			if ( chunk )
				out.add ( reinterpret_cast < const char* > ( data ), chunk );

			out.add ( static_cast < char > ( type ) );
			out.repeat ( '\0', AES_GCM_TAG_BYTES );

			uint8_t* bytes = reinterpret_cast < uint8_t* > ( out.data );

			record_nonce ( traffic, nonce );
			base::aes_gcm_seal ( traffic.key, nonce, bytes + start, TLS_HEADER, bytes + body, inner, bytes + body );
			traffic.sequence++;
			data += chunk;
			size -= chunk;

		} while ( size );

	}

	bool tls_open_record ( tls_traffic& traffic, const uint8_t* header, const uint8_t* body, size_t size, uint8_t* plain, size_t& plain_size, uint8_t& type ) {

		uint8_t nonce [ 12 ];

		if ( size < AES_GCM_TAG_BYTES + 1 || size > TLS_CIPHER_LIMIT )
			return false;

		record_nonce ( traffic, nonce );

		if ( !base::aes_gcm_open ( traffic.key, nonce, header, TLS_HEADER, body, size, plain ) )
			return false;

		traffic.sequence++;

		size_t at = size - AES_GCM_TAG_BYTES;

		while ( at > 0 && plain [ at - 1 ] == 0 )
			at--;

		if ( !at )
			return false;

		type = plain [ at - 1 ];
		plain_size = at - 1;

		return plain_size <= TLS_RECORD_LIMIT;

	}

	static void next_secret ( tls_traffic& traffic ) {

		uint8_t next [ SHA256_BYTES ];

		base::hkdf_label ( traffic.secret, "traffic upd", nullptr, 0, next, SHA256_BYTES );
		tls_keys ( traffic, next );

		for ( size_t i = 0; i < SHA256_BYTES; i++ )
			next [ i ] = 0;

	}

	static bool handle_post_handshake ( server_connection& link, const uint8_t* data, size_t size ) {

		tls_session& session = *link.tls;

		session.pending_handshake.add ( reinterpret_cast < const char* > ( data ), size );

		while ( session.pending_handshake.size >= 4 ) {

			const uint8_t* message = reinterpret_cast < const uint8_t* > ( session.pending_handshake.value ( ) );
			size_t length = static_cast < size_t > ( message [ 1 ] ) << 16 | static_cast < size_t > ( message [ 2 ] ) << 8 | message [ 3 ];

			if ( length > TLS_HANDSHAKE_LIMIT )
				return false;

			if ( session.pending_handshake.size < 4 + length )
				return true;

			if ( message [ 0 ] != 24 || length != 1 || message [ 4 ] > 1 )
				return false;

			next_secret ( session.reading );

			if ( message [ 4 ] == 1 )
				session.update_owed = true;

			size_t used = 4 + length;

			base::move ( session.pending_handshake.data, session.pending_handshake.data + used, session.pending_handshake.size - used );
			session.pending_handshake.size -= used;

		}

		return true;

	}

	static bool unseal ( server_connection& link ) {

		tls_session& session = *link.tls;

		while ( session.raw_size >= TLS_HEADER ) {

			size_t size = static_cast < size_t > ( session.raw [ 3 ] ) << 8 | session.raw [ 4 ];

			if ( size > TLS_CIPHER_LIMIT )
				return false;

			if ( session.raw_size < TLS_HEADER + size )
				return true;

			uint8_t kind = session.raw [ 0 ];

			if ( kind == TLS_CHANGE_CIPHER && size == 1 ) {

			} else if ( kind != TLS_APPLICATION )
				return false;
			else {

				if ( !reserve_input ( link, size ) )
					return false;

				size_t plain_size = 0;
				uint8_t type = 0;
				uint8_t* plain = link.input + link.input_end;

				if ( !tls_open_record ( session.reading, session.raw, session.raw + TLS_HEADER, size, plain, plain_size, type ) )
					return false;

				if ( type == TLS_APPLICATION )
					link.input_end += plain_size;
				else if ( type == TLS_ALERT ) {

					session.ended = true;

					return false;

				} else if ( type == TLS_HANDSHAKE ) {

					if ( !handle_post_handshake ( link, plain, plain_size ) )
						return false;

				} else
					return false;

			}

			size_t used = TLS_HEADER + size;

			base::move ( session.raw, session.raw + used, session.raw_size - used );
			session.raw_size -= used;

		}

		return true;

	}

	void tls_receive ( server_connection& link ) {

		tls_session& session = *link.tls;
		size_t got = 0;
		bool closed = false;

		if ( !base::os::receive_now ( link.socket, session.raw + session.raw_size, TLS_RAW_ROOM - session.raw_size, got, closed ) || closed )
			link.closed = true;

		session.raw_size += got;

		if ( !unseal ( link ) )
			link.closed = true;

	}

	bool tls_take_leftover ( server_connection& link ) {

		tls_session& session = *link.tls;
		size_t left = link.input_end - link.input_start;

		if ( left > TLS_RAW_ROOM )
			return false;

		base::copy ( session.raw, link.input + link.input_start, left );
		session.raw_size = left;
		link.input_start = 0;
		link.input_end = 0;

		return unseal ( link );

	}

	bool tls_seal_output ( server_connection& link ) {

		tls_session& session = *link.tls;

		if ( session.update_owed ) {

			uint8_t message [ 5 ] = { 24, 0, 0, 1, 0 };

			tls_seal_record ( session.writing, TLS_HANDSHAKE, message, sizeof ( message ), session.sealed );
			next_secret ( session.writing );
			session.update_owed = false;

		}

		if ( link.output_sent < link.output.size )
			tls_seal_record ( session.writing, TLS_APPLICATION, reinterpret_cast < const uint8_t* > ( link.output.value ( ) ) + link.output_sent, link.output.size - link.output_sent, session.sealed );

		link.output.clear ( );
		link.output_sent = 0;

		return true;

	}

	bool tls_waiting ( const server_connection& link ) {

		return link.tls && link.tls->sealed_sent < link.tls->sealed.size;

	}

	void tls_release ( server_connection& link ) {

		if ( !link.tls )
			return;

		base::aes_gcm_forget ( link.tls->reading.key );
		base::aes_gcm_forget ( link.tls->writing.key );

		volatile uint8_t* secrets [ 2 ] = { link.tls->reading.secret, link.tls->writing.secret };

		for ( volatile uint8_t* secret : secrets )
			for ( size_t i = 0; i < SHA256_BYTES; i++ )
				secret [ i ] = 0;

		link.tls->sealed.~text ( );
		link.tls->pending_handshake.~text ( );
		base::release ( link.tls );
		link.tls = nullptr;

	}

}
