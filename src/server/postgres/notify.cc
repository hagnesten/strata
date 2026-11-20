#include "server/server.hh"

namespace server {

	static size_t skip_space ( const char* text, size_t at, size_t size ) {

		while ( at < size && ( text [ at ] == ' ' || text [ at ] == '\t' || text [ at ] == '\r' || text [ at ] == '\n' ) )
			at++;

		return at;

	}

	static bool take_channel ( const char* text, size_t& at, size_t size, char* out, size_t room ) {

		size_t length = 0;

		at = skip_space ( text, at, size );

		if ( at < size && text [ at ] == '"' ) {

			at++;

			while ( at < size ) {

				if ( text [ at ] == '"' ) {

					if ( at + 1 < size && text [ at + 1 ] == '"' )
						at++;
					else
						break;

				}

				if ( length + 1 >= room )
					return false;

				out [ length++ ] = text [ at++ ];

			}

			if ( at >= size )
				return false;

			at++;

		} else {

			while ( at < size && ( ( text [ at ] >= 'a' && text [ at ] <= 'z' ) || ( text [ at ] >= 'A' && text [ at ] <= 'Z' ) || ( text [ at ] >= '0' && text [ at ] <= '9' ) || text [ at ] == '_' || text [ at ] == '$' || text [ at ] == '.' ) ) {

				if ( length + 1 >= room )
					return false;

				out [ length++ ] = base::lower ( text [ at++ ] );

			}

		}

		out [ length ] = 0;

		return length > 0;

	}

	static void send_notice ( server_connection& target, uint32_t sender, const char* channel, const char* payload, size_t payload_size ) {

		for ( uint32_t i = 0; i < target.channel_count; i++ ) {

			if ( !base::same ( target.channels [ i ], channel ) )
				continue;

			begin_message ( target, 'A' );
			put32 ( target, static_cast < int32_t > ( sender ) );
			put_string ( target, channel );
			put_bytes ( target, payload, payload_size );
			put_bytes ( target, "\0", 1 );
			end_message ( target );

			return;

		}

	}

	static void broadcast ( server_connection& link, const char* channel, const char* payload, size_t payload_size ) {

		server_state& state = *link.owner;

		for ( uint32_t i = 0; i < state.connection_count; i++ )
			if ( !state.connections [ i ]->closed )
				send_notice ( *state.connections [ i ], link.id, channel, payload, payload_size );

	}

	void deliver_notices ( server_connection& link, bool committed ) {

		const char* at = link.notices.value ( );
		const char* end = at + link.notices.size;

		while ( committed && at < end ) {

			const char* channel = at;
			size_t channel_size = base::length ( channel );
			const char* payload = channel + channel_size + 1;
			size_t payload_size = base::length ( payload );

			broadcast ( link, channel, payload, payload_size );
			at = payload + payload_size + 1;

		}

		link.notices.clear ( );

	}

	bool listen_command ( server_connection& link, const char* tag, const char* text, size_t size, base::text& problem ) {

		char channel [ SERVER_NAME_ROOM ];
		size_t at = 0;

		if ( base::same ( tag, "UNLISTEN" ) ) {

			at = skip_space ( text, at, size );

			if ( at < size && text [ at ] == '*' ) {

				link.channel_count = 0;

				return true;

			}

			if ( !take_channel ( text, at, size, channel, sizeof ( channel ) ) ) {

				problem.add ( "UNLISTEN needs a channel name or *" );

				return false;

			}

			for ( uint32_t i = 0; i < link.channel_count; i++ ) {

				if ( base::same ( link.channels [ i ], channel ) ) {

					base::copy_text ( link.channels [ i ], SERVER_NAME_ROOM, link.channels [ --link.channel_count ] );

					break;

				}

			}

			return true;

		}

		if ( !take_channel ( text, at, size, channel, sizeof ( channel ) ) ) {

			problem.add ( "expected a channel name of at most 63 characters" );

			return false;

		}

		if ( base::same ( tag, "LISTEN" ) ) {

			for ( uint32_t i = 0; i < link.channel_count; i++ )
				if ( base::same ( link.channels [ i ], channel ) )
					return true;

			if ( link.channel_count == SERVER_CHANNELS ) {

				problem.add ( "a connection can listen on at most 64 channels" );

				return false;

			}

			base::copy_text ( link.channels [ link.channel_count++ ], SERVER_NAME_ROOM, channel );

			return true;

		}

		base::text payload;

		at = skip_space ( text, at, size );

		if ( at < size && text [ at ] == ',' ) {

			at = skip_space ( text, at + 1, size );

			if ( at >= size || text [ at ] != '\'' ) {

				problem.add ( "the NOTIFY payload must be a quoted string" );

				return false;

			}

			at++;

			while ( at < size ) {

				if ( text [ at ] == '\'' ) {

					if ( at + 1 < size && text [ at + 1 ] == '\'' )
						at++;
					else
						break;

				}

				if ( !text [ at ] ) {

					problem.add ( "the NOTIFY payload cannot hold a zero byte" );

					return false;

				}

				payload.add ( text [ at++ ] );

			}

			if ( at >= size ) {

				problem.add ( "the NOTIFY payload is missing its closing quote" );

				return false;

			}

		}

		if ( payload.size >= SERVER_NOTICE_ROOM ) {

			problem.add ( "the NOTIFY payload is longer than 8000 bytes" );

			return false;

		}

		if ( link.db && strata_in_transaction ( link.db ) ) {

			if ( link.notices.size + payload.size > 1048576 ) {

				problem.add ( "too many notifications are waiting in this transaction" );

				return false;

			}

			link.notices.add ( channel ).add ( '\0' ).add ( payload.value ( ), payload.size ).add ( '\0' );

			return true;

		}

		broadcast ( link, channel, payload.value ( ), payload.size );

		return true;

	}

}
