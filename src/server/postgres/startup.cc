#include "server/server.hh"
#include "base/text/base64.hh"
#include "base/hash/scram.hh"

namespace server {

	static void send_fatal ( server_connection& link, const char* code, const char* message ) {

		begin_message ( link, 'E' );
		put_bytes ( link, "S", 1 );
		put_string ( link, "FATAL" );
		put_bytes ( link, "V", 1 );
		put_string ( link, "FATAL" );
		put_bytes ( link, "C", 1 );
		put_string ( link, code );
		put_bytes ( link, "M", 1 );
		put_string ( link, message );
		put_bytes ( link, "\0", 1 );
		end_message ( link );
		flush_output ( link );

	}

	static void send_auth ( server_connection& link, int32_t kind, const void* data, size_t size ) {

		begin_message ( link, 'R' );
		put32 ( link, kind );

		if ( size )
			put_bytes ( link, data, size );

		end_message ( link );

	}

	static bool sasl_message ( server_connection& link, pg_message& message ) {

		if ( !read_message ( link, message ) )
			return false;

		if ( message.type != 'p' ) {

			send_fatal ( link, "08P01", "expected a SASL response" );

			return false;

		}

		return true;

	}

	static bool scram_exchange ( server_connection& link, const server_user& user ) {

		send_auth ( link, 10, "SCRAM-SHA-256\0\0", 15 );

		if ( !flush_output ( link ) )
			return false;

		pg_message message;

		if ( !sasl_message ( link, message ) )
			return false;

		pg_reader reader = { message.body, message.body + message.size, false };
		const char* mechanism = read_string ( reader );
		int32_t size = read32 ( reader );
		const char* first = reinterpret_cast < const char* > ( read_span ( reader, size > 0 ? static_cast < size_t > ( size ) : 0 ) );

		if ( reader.failed || !mechanism || !base::same ( mechanism, "SCRAM-SHA-256" ) || !first || size < 3 || ( first [ 0 ] != 'n' && first [ 0 ] != 'y' ) ) {

			send_fatal ( link, "28000", "only SCRAM-SHA-256 sign-in without channel binding is supported" );

			return false;

		}

		const char* bare = first;
		const char* end = first + size;
		uint32_t commas = 0;

		while ( bare < end && commas < 2 )
			if ( *bare++ == ',' )
				commas++;

		const char* nonce = nullptr;
		size_t nonce_size = 0;

		if ( !base::scram_field ( bare, static_cast < size_t > ( end - bare ), 'r', nonce, nonce_size ) || !nonce_size ) {

			send_fatal ( link, "08P01", "the SCRAM first message has no nonce" );

			return false;

		}

		uint8_t random [ 18 ];
		base::text server_first;

		base::os::random_bytes ( random, sizeof ( random ) );
		server_first.add ( "r=" ).add ( nonce, nonce_size );
		base::add_base64 ( server_first, random, sizeof ( random ) );
		server_first.add ( ",s=" );
		base::add_base64 ( server_first, user.salt, SERVER_SALT );
		server_first.add ( ",i=" ).number ( user.iterations );

		base::text client_bare;

		client_bare.add ( bare, static_cast < size_t > ( end - bare ) );
		send_auth ( link, 11, server_first.value ( ), server_first.size );

		if ( !flush_output ( link ) || !sasl_message ( link, message ) )
			return false;

		const char* final = reinterpret_cast < const char* > ( message.body );
		size_t final_size = message.size;
		const char* proof_text = nullptr;
		size_t proof_size = 0;
		const char* echoed = nullptr;
		size_t echoed_size = 0;

		if ( !base::scram_field ( final, final_size, 'p', proof_text, proof_size ) || !base::scram_field ( final, final_size, 'r', echoed, echoed_size ) ) {

			send_fatal ( link, "08P01", "the SCRAM final message is incomplete" );

			return false;

		}

		size_t expected_nonce = server_first.size;

		for ( size_t i = 0; i < server_first.size; i++ ) {

			if ( server_first.data [ i ] == ',' ) {

				expected_nonce = i - 2;

				break;

			}

		}

		size_t without_proof = static_cast < size_t > ( proof_text - final ) - 3;
		uint8_t proof [ SHA256_BYTES ];
		size_t proof_got = 0;
		bool fine = echoed_size == expected_nonce && base::same_bytes ( echoed, server_first.value ( ) + 2, expected_nonce );

		fine = fine && base::read_base64 ( proof_text, proof_size, proof, sizeof ( proof ), proof_got ) && proof_got == SHA256_BYTES;

		base::text auth;

		auth.add ( client_bare.value ( ), client_bare.size ).add ( ',' ).add ( server_first.value ( ), server_first.size ).add ( ',' ).add ( final, without_proof );

		uint8_t signature [ SHA256_BYTES ];
		uint8_t client_key [ SHA256_BYTES ];
		uint8_t stored [ SHA256_BYTES ];

		base::hmac_sha256 ( user.stored, SHA256_BYTES, auth.value ( ), auth.size, signature );

		for ( uint32_t i = 0; i < SHA256_BYTES; i++ )
			client_key [ i ] = proof [ i ] ^ signature [ i ];

		base::sha256 ( client_key, SHA256_BYTES, stored );
		fine = fine && base::same_bytes ( stored, user.stored, SHA256_BYTES );

		if ( !fine ) {

			base::text line;

			line.add ( "password authentication failed for user \"" ).add ( link.user ).add ( '"' );
			send_fatal ( link, "28P01", line.value ( ) );

			return false;

		}

		uint8_t server_signature [ SHA256_BYTES ];
		base::text server_final;

		base::hmac_sha256 ( user.check, SHA256_BYTES, auth.value ( ), auth.size, server_signature );
		server_final.add ( "v=" );
		base::add_base64 ( server_final, server_signature, SHA256_BYTES );
		send_auth ( link, 12, server_final.value ( ), server_final.size );

		return true;

	}

	static void parameter ( server_connection& link, const char* name, const char* value ) {

		begin_message ( link, 'S' );
		put_string ( link, name );
		put_string ( link, value );
		end_message ( link );

	}

	bool sign_in ( server_connection& link ) {

		for ( ;; ) {

			if ( !need_bytes ( link, 8 ) )
				return false;

			const uint8_t* head = link.input + link.input_start;
			uint32_t size = static_cast < uint32_t > ( head [ 0 ] ) << 24 | static_cast < uint32_t > ( head [ 1 ] ) << 16 | static_cast < uint32_t > ( head [ 2 ] ) << 8 | head [ 3 ];

			if ( size < 8 || size > 10000 || !need_bytes ( link, size ) )
				return false;

			head = link.input + link.input_start;

			pg_reader reader = { head + 4, head + size, false };
			int32_t code = read32 ( reader );

			link.input_start += size;

			if ( code == PG_SSL_REQUEST || code == PG_GSS_REQUEST ) {

				put_bytes ( link, "N", 1 );

				if ( !flush_output ( link ) )
					return false;

				continue;

			}

			if ( code == PG_CANCEL_REQUEST )
				return false;

			if ( code != PG_PROTOCOL ) {

				send_fatal ( link, "0A000", "only version 3 of the Postgres protocol is supported" );

				return false;

			}

			while ( !reader.failed && reader.at < reader.end && *reader.at ) {

				const char* key = read_string ( reader );
				const char* value = read_string ( reader );

				if ( !key || !value )
					break;

				if ( base::same ( key, "user" ) )
					base::copy_text ( link.user, sizeof ( link.user ), value );
				else if ( base::same ( key, "database" ) )
					base::copy_text ( link.database, sizeof ( link.database ), value );

			}

			break;

		}

		server_state& state = *link.owner;

		if ( state.user_count ) {

			server_user* user = nullptr;

			if ( !find_user ( state, link.user, user ) ) {

				base::text line;

				line.add ( "password authentication failed for user \"" ).add ( link.user ).add ( '"' );
				send_fatal ( link, "28P01", line.value ( ) );

				return false;

			}

			if ( !scram_exchange ( link, *user ) )
				return false;

		}

		if ( strata_open ( state.settings.path, &link.db ) != STRATA_OK ) {

			send_fatal ( link, "58000", link.db ? strata_error ( link.db ) : "the database could not be opened" );

			return false;

		}

		strata_busy_timeout ( link.db, 0 );
		send_auth ( link, 0, nullptr, 0 );
		parameter ( link, "server_version", "16.0" );
		parameter ( link, "server_encoding", "UTF8" );
		parameter ( link, "client_encoding", "UTF8" );
		parameter ( link, "DateStyle", "ISO, MDY" );
		parameter ( link, "IntervalStyle", "postgres" );
		parameter ( link, "TimeZone", "UTC" );
		parameter ( link, "integer_datetimes", "on" );
		parameter ( link, "standard_conforming_strings", "on" );
		parameter ( link, "is_superuser", "on" );
		parameter ( link, "session_authorization", link.user );
		parameter ( link, "application_name", "" );
		begin_message ( link, 'K' );
		put32 ( link, static_cast < int32_t > ( link.id ) );
		put32 ( link, static_cast < int32_t > ( link.secret ) );
		end_message ( link );
		send_ready ( link );

		return flush_output ( link );

	}

	void send_ready ( server_connection& link ) {

		begin_message ( link, 'Z' );
		put_bytes ( link, link.db && strata_in_transaction ( link.db ) ? "T" : "I", 1 );
		end_message ( link );

	}

	void send_error ( server_connection& link, const char* code, const char* message ) {

		begin_message ( link, 'E' );
		put_bytes ( link, "S", 1 );
		put_string ( link, "ERROR" );
		put_bytes ( link, "V", 1 );
		put_string ( link, "ERROR" );
		put_bytes ( link, "C", 1 );
		put_string ( link, code );
		put_bytes ( link, "M", 1 );
		put_string ( link, message );
		put_bytes ( link, "\0", 1 );
		end_message ( link );

	}

	void serve_connection ( void* context ) {

		server_connection& link = *static_cast < server_connection* > ( context );

		if ( !sign_in ( link ) ) {

			link.finished = true;

			return;

		}

		for ( ;; ) {

			pg_message message;

			if ( !read_message ( link, message ) )
				break;

			bool fine = true;

			switch ( message.type ) {

				case 'Q':
					fine = handle_simple ( link, message );
					break;

				case 'X':
					link.finished = true;

					return;

				case 'P':
				case 'B':
				case 'D':
				case 'E':
				case 'S':
				case 'C':
				case 'H':
					fine = handle_extended ( link, message );
					break;

				default:
					send_error ( link, "08P01", "this message type is not supported" );
					break;

			}

			if ( !fine )
				break;

			if ( message.type == 'Q' || message.type == 'S' || message.type == 'H' || link.output.size > SERVER_INPUT )
				if ( !flush_output ( link ) )
					break;

		}

		link.finished = true;

	}

	uint8_t read8 ( pg_reader& reader ) {

		if ( reader.at + 1 > reader.end ) {

			reader.failed = true;

			return 0;

		}

		return *reader.at++;

	}

	int16_t read16 ( pg_reader& reader ) {

		if ( reader.at + 2 > reader.end ) {

			reader.failed = true;

			return 0;

		}

		int16_t value = static_cast < int16_t > ( reader.at [ 0 ] << 8 | reader.at [ 1 ] );

		reader.at += 2;

		return value;

	}

	int32_t read32 ( pg_reader& reader ) {

		if ( reader.at + 4 > reader.end ) {

			reader.failed = true;

			return 0;

		}

		int32_t value = static_cast < int32_t > ( static_cast < uint32_t > ( reader.at [ 0 ] ) << 24 | static_cast < uint32_t > ( reader.at [ 1 ] ) << 16 | static_cast < uint32_t > ( reader.at [ 2 ] ) << 8 | reader.at [ 3 ] );

		reader.at += 4;

		return value;

	}

	const char* read_string ( pg_reader& reader ) {

		const uint8_t* start = reader.at;

		while ( reader.at < reader.end && *reader.at )
			reader.at++;

		if ( reader.at >= reader.end ) {

			reader.failed = true;

			return nullptr;

		}

		reader.at++;

		return reinterpret_cast < const char* > ( start );

	}

	const uint8_t* read_span ( pg_reader& reader, size_t size ) {

		if ( static_cast < size_t > ( reader.end - reader.at ) < size ) {

			reader.failed = true;

			return nullptr;

		}

		const uint8_t* start = reader.at;

		reader.at += size;

		return start;

	}

}
