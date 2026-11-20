#include "bench/postgres/wire.hh"
#include "base/hash/scram.hh"
#include "base/text/base64.hh"
#include "base/platform/os.hh"

namespace bench {

	static bool fail ( wire_link& link, const char* what ) {

		wire_fail ( link, what );

		return false;

	}

	static bool offers_scram ( const wire_message& message ) {

		uint32_t at = 4;

		while ( at < message.size && message.body [ at ] ) {

			const char* name = reinterpret_cast < const char* > ( message.body + at );

			if ( base::same ( name, "SCRAM-SHA-256" ) )
				return true;

			at += static_cast < uint32_t > ( base::length ( name ) ) + 1;

		}

		return false;

	}

	static bool start_scram ( wire_link& link, const wire_settings& settings, sign_in_state& state ) {

		uint8_t random [ 18 ];

		if ( !settings.password [ 0 ] )
			return fail ( link, "Postgres wants a password: put password= in the connection string or set PGPASSWORD" );

		if ( !base::os::random_bytes ( random, sizeof ( random ) ) )
			return fail ( link, "the system could not make a random sign-in nonce" );

		base::add_base64 ( state.nonce, random, sizeof ( random ) );
		state.client_first.add ( "n=,r=" ).add ( state.nonce.value ( ), state.nonce.size );

		wire_begin ( link, 'p' );
		wire_string ( link, "SCRAM-SHA-256" );
		wire_int32 ( link, static_cast < int32_t > ( state.client_first.size + 3 ) );
		wire_bytes ( link, "n,,", 3 );
		wire_bytes ( link, state.client_first.data, state.client_first.size );
		wire_end ( link );

		return wire_flush ( link );

	}

	static bool answer_scram ( wire_link& link, const wire_settings& settings, sign_in_state& state, const wire_message& message ) {

		const char* server_first = reinterpret_cast < const char* > ( message.body + 4 );
		size_t server_size = message.size - 4;
		const char* nonce = nullptr;
		size_t nonce_size = 0;

		if ( !base::scram_field ( server_first, server_size, 'r', nonce, nonce_size ) || nonce_size <= state.nonce.size || !base::same_bytes ( nonce, state.nonce.data, state.nonce.size ) )
			return fail ( link, "Postgres answered the sign-in with a nonce that does not match" );

		state.client_final.add ( "c=biws,r=" ).add ( nonce, nonce_size );

		base::scram_answer answer;

		if ( !base::scram_prove ( settings.password, base::length ( settings.password ), state.client_first.data, state.client_first.size, server_first, server_size, state.client_final.data, state.client_final.size, answer ) )
			return fail ( link, "Postgres sent a sign-in challenge Strata could not read" );

		base::copy ( state.server, answer.server, SHA256_BYTES );
		state.client_final.add ( ",p=" );
		base::add_base64 ( state.client_final, answer.proof, SHA256_BYTES );

		wire_begin ( link, 'p' );
		wire_bytes ( link, state.client_final.data, state.client_final.size );
		wire_end ( link );

		return wire_flush ( link );

	}

	static bool check_scram ( wire_link& link, sign_in_state& state, const wire_message& message ) {

		const char* text = reinterpret_cast < const char* > ( message.body + 4 );
		size_t size = message.size - 4;
		const char* value = nullptr;
		size_t value_size = 0;
		uint8_t signature [ SHA256_BYTES ];
		size_t got = 0;

		if ( !base::scram_field ( text, size, 'v', value, value_size ) || !base::read_base64 ( value, value_size, signature, sizeof ( signature ), got ) || got != SHA256_BYTES || !base::same_bytes ( signature, state.server, SHA256_BYTES ) )
			return fail ( link, "the Postgres server could not prove it knows the password" );

		state.proved = true;

		return true;

	}

	static bool authenticate ( wire_link& link, const wire_settings& settings, sign_in_state& state, const wire_message& message ) {

		if ( message.size < 4 )
			return fail ( link, "Postgres sent a malformed sign-in message" );

		uint32_t code = wire_read32 ( message.body );

		switch ( code ) {

			case WIRE_AUTH_OK:
				return true;

			case WIRE_AUTH_CLEARTEXT:

				if ( !settings.password [ 0 ] )
					return fail ( link, "Postgres wants a password: put password= in the connection string or set PGPASSWORD" );

				wire_begin ( link, 'p' );
				wire_string ( link, settings.password );
				wire_end ( link );

				return wire_flush ( link );

			case WIRE_AUTH_SASL:

				if ( !offers_scram ( message ) )
					return fail ( link, "Postgres offered no SCRAM-SHA-256 sign-in" );

				return start_scram ( link, settings, state );

			case WIRE_AUTH_SASL_CONTINUE:
				return answer_scram ( link, settings, state, message );

			case WIRE_AUTH_SASL_FINAL:
				return check_scram ( link, state, message );

			case WIRE_AUTH_MD5:
				return fail ( link, "Postgres wants an MD5 password; set password_encryption = 'scram-sha-256' and set the password again" );

			default:
				return fail ( link, "Postgres asked for a sign-in method Strata does not speak" );

		}

	}

	bool wire_open ( wire_link& link, const wire_settings& settings ) {

		if ( !base::os::connect_tcp ( link.socket, settings.host, settings.port ) ) {

			link.problem.clear ( );
			link.problem.add ( "Postgres at " ).add ( settings.host ).add ( ':' ).number ( settings.port ).add ( " could not be reached" );

			return false;

		}

		wire_begin ( link, 0 );
		wire_int32 ( link, WIRE_PROTOCOL );
		wire_string ( link, "user" );
		wire_string ( link, settings.user );
		wire_string ( link, "database" );
		wire_string ( link, settings.database );
		wire_string ( link, "application_name" );
		wire_string ( link, "strata-bench" );
		wire_string ( link, "client_encoding" );
		wire_string ( link, "UTF8" );
		wire_bytes ( link, "", 1 );
		wire_end ( link );

		if ( !wire_flush ( link ) )
			return false;

		sign_in_state state;
		wire_message message;

		state.proved = false;

		for ( ;; ) {

			if ( !wire_next ( link, message ) )
				return false;

			switch ( message.type ) {

				case 'R':

					if ( !authenticate ( link, settings, state, message ) )
						return false;

					break;

				case 'S': {

					const char* name = reinterpret_cast < const char* > ( message.body );

					if ( base::same ( name, "server_version" ) )
						base::copy_text ( link.server_version, sizeof ( link.server_version ), name + base::length ( name ) + 1 );

					break;

				}

				case 'E':
					wire_note_error ( link, message );
					return false;

				case 'Z':
					link.status = message.size ? message.body [ 0 ] : 'I';

					if ( state.nonce.size && !state.proved )
						return fail ( link, "Postgres skipped the end of the SCRAM sign-in" );

					return true;

				default:
					break;

			}

		}

	}

}
