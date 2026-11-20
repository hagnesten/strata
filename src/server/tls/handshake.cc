#include "server/server.hh"
#include "server/tls/tls.hh"
#include "base/crypto/hkdf.hh"
#include "base/crypto/x25519.hh"
#include "base/platform/os.hh"

#define ALERT_HANDSHAKE_FAILURE 40
#define ALERT_ILLEGAL_PARAMETER 47
#define ALERT_DECODE_ERROR 50
#define ALERT_DECRYPT_ERROR 51
#define ALERT_PROTOCOL_VERSION 70
#define ALERT_UNEXPECTED 10

namespace server {

	static uint32_t take ( tls_reader& reader, uint32_t bytes ) {

		if ( reader.bad || static_cast < size_t > ( reader.end - reader.at ) < bytes ) {

			reader.bad = true;

			return 0;

		}

		uint32_t value = 0;

		for ( uint32_t i = 0; i < bytes; i++ )
			value = value << 8 | *reader.at++;

		return value;

	}

	static tls_reader slice ( tls_reader& reader, uint32_t size ) {

		tls_reader part = { reader.at, reader.at, true };

		if ( reader.bad || static_cast < size_t > ( reader.end - reader.at ) < size ) {

			reader.bad = true;

			return part;

		}

		part.end = reader.at + size;
		part.bad = false;
		reader.at += size;

		return part;

	}

	static bool read_hello ( const uint8_t* data, size_t size, client_hello& out ) {

		tls_reader reader = { data, data + size, false };

		out = { };
		take ( reader, 2 );
		slice ( reader, 32 );

		tls_reader session = slice ( reader, take ( reader, 1 ) );

		if ( session.bad || session.end - session.at > 32 )
			return false;

		out.session_size = static_cast < uint32_t > ( session.end - session.at );

		for ( uint32_t i = 0; i < out.session_size; i++ )
			out.session [ i ] = session.at [ i ];

		tls_reader ciphers = slice ( reader, take ( reader, 2 ) );

		while ( !ciphers.bad && ciphers.at < ciphers.end )
			if ( take ( ciphers, 2 ) == 0x1301 )
				out.cipher = true;

		slice ( reader, take ( reader, 1 ) );

		tls_reader extensions = slice ( reader, take ( reader, 2 ) );

		while ( !extensions.bad && extensions.at < extensions.end ) {

			uint32_t kind = take ( extensions, 2 );
			tls_reader body = slice ( extensions, take ( extensions, 2 ) );

			if ( kind == 43 ) {

				tls_reader versions = slice ( body, take ( body, 1 ) );

				while ( !versions.bad && versions.at < versions.end )
					if ( take ( versions, 2 ) == 0x0304 )
						out.modern = true;

			} else if ( kind == 51 ) {

				tls_reader shares = slice ( body, take ( body, 2 ) );

				while ( !shares.bad && shares.at < shares.end ) {

					uint32_t group = take ( shares, 2 );
					tls_reader key = slice ( shares, take ( shares, 2 ) );

					if ( group == 0x001D && !key.bad && key.end - key.at == 32 ) {

						for ( int i = 0; i < 32; i++ )
							out.share [ i ] = key.at [ i ];

						out.has_share = true;

					}

				}

				if ( shares.bad )
					return false;

			} else if ( kind == 13 ) {

				tls_reader schemes = slice ( body, take ( body, 2 ) );

				while ( !schemes.bad && schemes.at < schemes.end )
					if ( take ( schemes, 2 ) == 0x0403 )
						out.signature = true;

			} else if ( kind == 16 ) {

				tls_reader names = slice ( body, take ( body, 2 ) );

				out.alpn_offered = true;

				while ( !names.bad && names.at < names.end ) {

					tls_reader name = slice ( names, take ( names, 1 ) );

					if ( !name.bad && name.end - name.at == 10 && base::same_bytes ( name.at, "postgresql", 10 ) )
						out.alpn_postgres = true;

				}

			}

		}

		return !reader.bad && !extensions.bad;

	}

	static void put_number ( base::text& out, uint32_t value, uint32_t bytes ) {

		for ( uint32_t i = bytes; i-- > 0; )
			out.add ( static_cast < char > ( value >> ( 8 * i ) ) );

	}

	static void message ( base::text& out, uint8_t type, const base::text& body ) {

		out.add ( static_cast < char > ( type ) );
		put_number ( out, static_cast < uint32_t > ( body.size ), 3 );
		out.add ( body.value ( ), body.size );

	}

	static void transcript_hash ( const base::sha256_state& transcript, uint8_t out [ SHA256_BYTES ] ) {

		base::sha256_state copy = transcript;

		base::sha256_finish ( copy, out );

	}

	static void derive ( const uint8_t secret [ SHA256_BYTES ], const char* label, const uint8_t hash [ SHA256_BYTES ], uint8_t out [ SHA256_BYTES ] ) {

		base::hkdf_label ( secret, label, hash, SHA256_BYTES, out, SHA256_BYTES );

	}

	static void finished_mac ( const uint8_t base_secret [ SHA256_BYTES ], const uint8_t hash [ SHA256_BYTES ], uint8_t out [ SHA256_BYTES ] ) {

		uint8_t key [ SHA256_BYTES ];

		base::hkdf_label ( base_secret, "finished", nullptr, 0, key, SHA256_BYTES );
		base::hmac_sha256 ( key, SHA256_BYTES, hash, SHA256_BYTES, out );

	}

	static bool alert ( server_connection& link, uint8_t description ) {

		if ( link.tls && link.tls->writing.sequence == 0 && !link.tls->active ) {

			const char bytes [ 7 ] = { TLS_ALERT, 3, 3, 0, 2, 2, static_cast < char > ( description ) };

			put_bytes ( link, bytes, sizeof ( bytes ) );
			flush_output ( link );

		}

		return false;

	}

	static bool read_record ( server_connection& link, uint8_t header [ TLS_HEADER ], base::text& body ) {

		if ( !need_bytes ( link, TLS_HEADER ) )
			return false;

		const uint8_t* head = link.input + link.input_start;
		size_t size = static_cast < size_t > ( head [ 3 ] ) << 8 | head [ 4 ];

		if ( size > TLS_CIPHER_LIMIT || !need_bytes ( link, TLS_HEADER + size ) )
			return false;

		head = link.input + link.input_start;

		for ( int i = 0; i < TLS_HEADER; i++ )
			header [ i ] = head [ i ];

		body.clear ( );
		body.add ( reinterpret_cast < const char* > ( head + TLS_HEADER ), size );
		link.input_start += TLS_HEADER + size;

		return true;

	}

	static bool read_client_hello ( server_connection& link, base::text& hello ) {

		uint8_t header [ TLS_HEADER ];
		base::text body;

		hello.clear ( );

		for ( ;; ) {

			if ( !read_record ( link, header, body ) )
				return false;

			if ( header [ 0 ] != TLS_HANDSHAKE )
				return alert ( link, ALERT_UNEXPECTED );

			hello.add ( body.value ( ), body.size );

			if ( hello.size >= 4 ) {

				const uint8_t* bytes = reinterpret_cast < const uint8_t* > ( hello.value ( ) );
				size_t length = static_cast < size_t > ( bytes [ 1 ] ) << 16 | static_cast < size_t > ( bytes [ 2 ] ) << 8 | bytes [ 3 ];

				if ( bytes [ 0 ] != 1 || length > TLS_HANDSHAKE_LIMIT )
					return alert ( link, ALERT_UNEXPECTED );

				if ( hello.size == 4 + length )
					return true;

				if ( hello.size > 4 + length )
					return alert ( link, ALERT_UNEXPECTED );

			}

		}

	}

	bool tls_accept ( server_connection& link ) {

		tls_identity* identity = link.owner->identity;

		if ( !identity || !identity->ready || !base::aes_gcm_supported ( ) )
			return false;

		link.tls = base::make < tls_session > ( 1 );

		if ( !link.tls )
			return false;

		tls_session& session = *link.tls;
		base::text hello;
		client_hello offer;

		if ( !read_client_hello ( link, hello ) )
			return false;

		if ( !read_hello ( reinterpret_cast < const uint8_t* > ( hello.value ( ) ) + 4, hello.size - 4, offer ) )
			return alert ( link, ALERT_DECODE_ERROR );

		if ( !offer.modern )
			return alert ( link, ALERT_PROTOCOL_VERSION );

		if ( !offer.cipher || !offer.has_share || !offer.signature )
			return alert ( link, ALERT_HANDSHAKE_FAILURE );

		base::sha256_state transcript;
		uint8_t ephemeral [ 32 ], ephemeral_public [ 32 ], shared [ 32 ], random [ 32 ];

		base::sha256_start ( transcript );
		base::sha256_add ( transcript, hello.value ( ), hello.size );

		if ( !base::os::random_bytes ( ephemeral, sizeof ( ephemeral ) ) || !base::os::random_bytes ( random, sizeof ( random ) ) )
			return alert ( link, ALERT_HANDSHAKE_FAILURE );

		base::x25519_public ( ephemeral, ephemeral_public );

		if ( !base::x25519 ( ephemeral, offer.share, shared ) )
			return alert ( link, ALERT_ILLEGAL_PARAMETER );

		for ( int i = 0; i < 32; i++ )
			ephemeral [ i ] = 0;

		base::text server_hello, body, extensions;

		put_number ( extensions, 43, 2 );
		put_number ( extensions, 2, 2 );
		put_number ( extensions, 0x0304, 2 );
		put_number ( extensions, 51, 2 );
		put_number ( extensions, 36, 2 );
		put_number ( extensions, 0x001D, 2 );
		put_number ( extensions, 32, 2 );
		extensions.add ( reinterpret_cast < const char* > ( ephemeral_public ), 32 );

		put_number ( body, 0x0303, 2 );
		body.add ( reinterpret_cast < const char* > ( random ), 32 );
		put_number ( body, offer.session_size, 1 );
		body.add ( reinterpret_cast < const char* > ( offer.session ), offer.session_size );
		put_number ( body, 0x1301, 2 );
		put_number ( body, 0, 1 );
		put_number ( body, static_cast < uint32_t > ( extensions.size ), 2 );
		body.add ( extensions.value ( ), extensions.size );
		message ( server_hello, 2, body );
		base::sha256_add ( transcript, server_hello.value ( ), server_hello.size );

		base::text wire;

		put_number ( wire, TLS_HANDSHAKE, 1 );
		put_number ( wire, 0x0303, 2 );
		put_number ( wire, static_cast < uint32_t > ( server_hello.size ), 2 );
		wire.add ( server_hello.value ( ), server_hello.size );
		wire.add ( "\x14\x03\x03\x00\x01\x01", 6 );

		uint8_t zero [ SHA256_BYTES ] = { };
		uint8_t empty_hash [ SHA256_BYTES ], early [ SHA256_BYTES ], derived [ SHA256_BYTES ], handshake [ SHA256_BYTES ], master [ SHA256_BYTES ];
		uint8_t hash [ SHA256_BYTES ], client_secret [ SHA256_BYTES ], server_secret [ SHA256_BYTES ];

		base::sha256 ( "", 0, empty_hash );
		base::hkdf_extract ( zero, SHA256_BYTES, zero, SHA256_BYTES, early );
		derive ( early, "derived", empty_hash, derived );
		base::hkdf_extract ( derived, SHA256_BYTES, shared, sizeof ( shared ), handshake );
		transcript_hash ( transcript, hash );
		derive ( handshake, "c hs traffic", hash, client_secret );
		derive ( handshake, "s hs traffic", hash, server_secret );
		derive ( handshake, "derived", empty_hash, derived );
		base::hkdf_extract ( derived, SHA256_BYTES, zero, SHA256_BYTES, master );

		for ( int i = 0; i < 32; i++ )
			shared [ i ] = 0;

		tls_keys ( session.reading, client_secret );
		tls_keys ( session.writing, server_secret );

		base::text flight, encrypted_extensions, certificate, verify, finished;

		body.clear ( );

		if ( offer.alpn_postgres ) {

			put_number ( body, 16, 2 );
			put_number ( body, 13, 2 );
			put_number ( body, 11, 2 );
			put_number ( body, 10, 1 );
			body.add ( "postgresql", 10 );

		}

		base::text holder;

		put_number ( holder, static_cast < uint32_t > ( body.size ), 2 );
		holder.add ( body.value ( ), body.size );
		message ( encrypted_extensions, 8, holder );
		base::sha256_add ( transcript, encrypted_extensions.value ( ), encrypted_extensions.size );

		body.clear ( );
		put_number ( body, 0, 1 );
		put_number ( body, static_cast < uint32_t > ( identity->chain.size ), 3 );
		body.add ( identity->chain.value ( ), identity->chain.size );
		message ( certificate, 11, body );
		base::sha256_add ( transcript, certificate.value ( ), certificate.size );

		transcript_hash ( transcript, hash );

		base::text signed_content;
		uint8_t digest [ SHA256_BYTES ], r [ 32 ], s [ 32 ];

		signed_content.repeat ( ' ', 64 );
		signed_content.add ( "TLS 1.3, server CertificateVerify" );
		signed_content.add ( '\0' );
		signed_content.add ( reinterpret_cast < const char* > ( hash ), SHA256_BYTES );
		base::sha256 ( signed_content.value ( ), signed_content.size, digest );

		if ( !base::p256_sign ( identity->secret, digest, r, s ) )
			return alert ( link, ALERT_HANDSHAKE_FAILURE );

		base::text signature, integers;
		uint8_t* numbers [ 2 ] = { r, s };

		for ( uint8_t* number : numbers ) {

			base::text value;
			uint32_t start = 0;

			while ( start < 31 && number [ start ] == 0 && !( number [ start + 1 ] & 0x80 ) )
				start++;

			if ( number [ start ] & 0x80 )
				value.add ( '\0' );

			value.add ( reinterpret_cast < const char* > ( number + start ), 32 - start );
			integers.add ( '\x02' ).add ( static_cast < char > ( value.size ) ).add ( value.value ( ), value.size );

		}

		signature.add ( '\x30' ).add ( static_cast < char > ( integers.size ) ).add ( integers.value ( ), integers.size );

		body.clear ( );
		put_number ( body, 0x0403, 2 );
		put_number ( body, static_cast < uint32_t > ( signature.size ), 2 );
		body.add ( signature.value ( ), signature.size );
		message ( verify, 15, body );
		base::sha256_add ( transcript, verify.value ( ), verify.size );

		uint8_t mac [ SHA256_BYTES ];

		transcript_hash ( transcript, hash );
		finished_mac ( server_secret, hash, mac );
		body.clear ( );
		body.add ( reinterpret_cast < const char* > ( mac ), SHA256_BYTES );
		message ( finished, 20, body );
		base::sha256_add ( transcript, finished.value ( ), finished.size );

		flight.add ( encrypted_extensions.value ( ), encrypted_extensions.size );
		flight.add ( certificate.value ( ), certificate.size );
		flight.add ( verify.value ( ), verify.size );
		flight.add ( finished.value ( ), finished.size );
		tls_seal_record ( session.writing, TLS_HANDSHAKE, reinterpret_cast < const uint8_t* > ( flight.value ( ) ), flight.size, wire );
		put_bytes ( link, wire.value ( ), wire.size );

		if ( !flush_output ( link ) )
			return false;

		uint8_t application_hash [ SHA256_BYTES ];

		transcript_hash ( transcript, application_hash );

		base::text record, collected;
		uint8_t header [ TLS_HEADER ];

		for ( ;; ) {

			if ( !read_record ( link, header, record ) )
				return false;

			if ( header [ 0 ] == TLS_CHANGE_CIPHER && record.size == 1 )
				continue;

			if ( header [ 0 ] != TLS_APPLICATION )
				return false;

			uint8_t plain [ TLS_CIPHER_LIMIT ];
			size_t plain_size = 0;
			uint8_t type = 0;

			if ( !tls_open_record ( session.reading, header, reinterpret_cast < const uint8_t* > ( record.value ( ) ), record.size, plain, plain_size, type ) || type != TLS_HANDSHAKE )
				return false;

			collected.add ( reinterpret_cast < const char* > ( plain ), plain_size );

			if ( collected.size >= 4 + SHA256_BYTES )
				break;

		}

		const uint8_t* client_finished = reinterpret_cast < const uint8_t* > ( collected.value ( ) );
		uint8_t expected [ SHA256_BYTES ];
		uint8_t difference = 0;

		finished_mac ( client_secret, application_hash, expected );

		if ( collected.size != 4 + SHA256_BYTES || client_finished [ 0 ] != 20 || client_finished [ 1 ] != 0 || client_finished [ 2 ] != 0 || client_finished [ 3 ] != SHA256_BYTES )
			return false;

		for ( int i = 0; i < SHA256_BYTES; i++ )
			difference |= expected [ i ] ^ client_finished [ 4 + i ];

		if ( difference )
			return false;

		derive ( master, "c ap traffic", application_hash, client_secret );
		derive ( master, "s ap traffic", application_hash, server_secret );
		tls_keys ( session.reading, client_secret );
		tls_keys ( session.writing, server_secret );

		for ( int i = 0; i < SHA256_BYTES; i++ ) {

			client_secret [ i ] = 0;
			server_secret [ i ] = 0;
			master [ i ] = 0;
			handshake [ i ] = 0;

		}

		session.active = true;

		return tls_take_leftover ( link );

	}

}
