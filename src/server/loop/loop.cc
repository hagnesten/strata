#include "server/server.hh"
#include "base/console/console.hh"
#include "base/clock/clock.hh"

namespace server {

	static uint64_t now_ms ( ) {

		return base::precise_ticks ( ) * 1000 / base::tick_rate ( );

	}

	static void close_connection ( server_state& state, uint32_t at ) {

		server_connection* link = state.connections [ at ];

		forget_statements ( *link );

		if ( link->db )
			strata_close ( link->db );

		base::os::close_socket ( link->socket );

		if ( link->routine )
			base::free_coroutine ( link->routine );

		base::release ( link->input );
		link->output.~text ( );
		base::release ( link );
		state.connections [ at ] = state.connections [ --state.connection_count ];

	}

	static void resume ( server_connection& link ) {

		if ( link.routine && !link.routine->finished )
			base::enter_coroutine ( link.routine );

		if ( link.routine && link.routine->finished )
			link.finished = true;

	}

	static void accept_waiting ( server_state& state ) {

		for ( ;; ) {

			void* socket = nullptr;
			bool accepted = false;

			if ( !base::os::accept_tcp ( state.listener, socket, accepted ) || !accepted )
				return;

			if ( state.connection_count == SERVER_CONNECTIONS ) {

				base::os::close_socket ( socket );

				continue;

			}

			server_connection* link = base::make < server_connection > ( 1 );
			uint8_t* input = base::make < uint8_t > ( SERVER_INPUT );

			if ( !link || !input ) {

				base::release ( link );
				base::release ( input );
				base::os::close_socket ( socket );

				continue;

			}

			link->owner = &state;
			link->socket = socket;
			link->input = input;
			link->input_room = SERVER_INPUT;
			link->id = ++state.next_id;
			base::os::random_bytes ( &link->secret, sizeof ( link->secret ) );
			link->routine = base::make_coroutine ( serve_connection, link );

			if ( !link->routine ) {

				base::release ( input );
				base::release ( link );
				base::os::close_socket ( socket );

				continue;

			}

			state.connections [ state.connection_count++ ] = link;
			resume ( *link );

		}

	}

	static void pump_output ( server_connection& link ) {

		while ( link.output_sent < link.output.size ) {

			size_t sent = 0;

			if ( !base::os::send_some ( link.socket, link.output.value ( ) + link.output_sent, link.output.size - link.output_sent, sent ) ) {

				link.closed = true;

				return;

			}

			if ( !sent )
				return;

			link.output_sent += sent;

		}

		link.output.clear ( );
		link.output_sent = 0;

	}

	static void pump_input ( server_connection& link ) {

		if ( link.input_start == link.input_end ) {

			link.input_start = 0;
			link.input_end = 0;

		}

		if ( link.input_end == link.input_room && link.input_start ) {

			base::move ( link.input, link.input + link.input_start, link.input_end - link.input_start );
			link.input_end -= link.input_start;
			link.input_start = 0;

		}

		if ( link.input_end == link.input_room ) {

			size_t grown = link.input_room * 2;
			uint8_t* made = base::make < uint8_t > ( grown );

			if ( !made ) {

				link.closed = true;

				return;

			}

			base::copy ( made, link.input, link.input_end );
			base::release ( link.input );
			link.input = made;
			link.input_room = grown;

		}

		size_t got = 0;
		bool closed = false;

		if ( !base::os::receive_now ( link.socket, link.input + link.input_end, link.input_room - link.input_end, got, closed ) || closed )
			link.closed = true;

		link.input_end += got;

	}

	int32_t run_server ( const server_settings& settings ) {

		server_state* state = base::make < server_state > ( 1 );

		if ( !state )
			return 1;

		state->settings = settings;

		if ( !load_users ( *state ) && !settings.trust ) {

			base::write_error ( "no users are set up, add one with: strata --user <file> <name> <password>, or start with --trust for a local test\n" );

			return 1;

		}

		strata_db* probe = nullptr;

		if ( strata_open ( settings.path, &probe ) != STRATA_OK ) {

			base::text line;

			line.add ( "the database could not be opened: " ).add ( probe ? strata_error ( probe ) : settings.path ).add ( '\n' );
			base::write_error ( line.value ( ) );

			if ( probe )
				strata_close ( probe );

			return 1;

		}

		strata_close ( probe );

		if ( !base::os::listen_tcp ( state->listener, settings.host, settings.port ) ) {

			base::text line;

			line.add ( "could not listen on port " ).number ( settings.port ).add ( ", is something else using it?\n" );
			base::write_error ( line.value ( ) );

			return 1;

		}

		if ( !settings.quiet ) {

			base::text line;

			line.add ( "strata is serving " ).add ( settings.path ).add ( " on " ).add ( settings.host && *settings.host ? settings.host : "0.0.0.0" ).add ( ':' ).number ( settings.port );
			line.add ( settings.trust && !state->user_count ? " without passwords\n" : " for Postgres clients\n" );
			base::write ( line );

		}

		base::os::socket_poll* polls = base::make < base::os::socket_poll > ( SERVER_CONNECTIONS + 1 );

		if ( !polls )
			return 1;

		state->running = true;

		while ( state->running ) {

			uint32_t count = 0;
			bool waiting = false;

			polls [ count++ ] = { state->listener, true, false, false, false, false };

			for ( uint32_t i = 0; i < state->connection_count; i++ ) {

				server_connection& link = *state->connections [ i ];

				polls [ count++ ] = { link.socket, link.reading, link.output_sent < link.output.size, false, false, false };
				waiting = waiting || link.busy_wait;

			}

			if ( !base::os::poll_sockets ( polls, count, waiting ? 2 : 1000 ) )
				break;

			if ( polls [ 0 ].readable )
				accept_waiting ( *state );

			for ( uint32_t i = 0; i < state->connection_count; i++ ) {

				server_connection& link = *state->connections [ i ];
				base::os::socket_poll& poll = polls [ i + 1 ];

				if ( i + 1 >= count )
					break;

				if ( poll.failed )
					link.closed = true;

				if ( poll.writable )
					pump_output ( link );

				if ( poll.readable && link.reading ) {

					pump_input ( link );
					resume ( link );

				} else if ( link.busy_wait )
					resume ( link );

				if ( link.output_sent < link.output.size && !link.closed )
					pump_output ( link );

			}

			for ( uint32_t i = state->connection_count; i-- > 0; ) {

				server_connection& link = *state->connections [ i ];

				if ( link.closed || ( link.finished && link.output_sent >= link.output.size ) )
					close_connection ( *state, i );

			}

		}

		while ( state->connection_count )
			close_connection ( *state, state->connection_count - 1 );

		base::os::close_socket ( state->listener );
		base::release ( polls );

		return 0;

	}

	bool need_bytes ( server_connection& link, size_t count ) {

		while ( link.input_end - link.input_start < count ) {

			if ( link.closed )
				return false;

			if ( link.output_sent < link.output.size && !flush_output ( link ) )
				return false;

			link.reading = true;
			base::leave_coroutine ( link.routine );
			link.reading = false;

		}

		return true;

	}

	bool flush_output ( server_connection& link ) {

		while ( !link.closed && link.output_sent < link.output.size ) {

			size_t sent = 0;

			if ( !base::os::send_some ( link.socket, link.output.value ( ) + link.output_sent, link.output.size - link.output_sent, sent ) ) {

				link.closed = true;

				return false;

			}

			link.output_sent += sent;

			if ( link.output_sent < link.output.size && link.output.size - link.output_sent > SERVER_OUTPUT_LIMIT ) {

				link.busy_wait = true;
				base::leave_coroutine ( link.routine );
				link.busy_wait = false;

			} else if ( !sent )
				break;

		}

		if ( link.output_sent >= link.output.size ) {

			link.output.clear ( );
			link.output_sent = 0;

		}

		return !link.closed;

	}

	bool wait_busy ( server_connection& link ) {

		uint64_t now = now_ms ( );

		if ( !link.busy_since )
			link.busy_since = now;

		if ( now - link.busy_since > SERVER_BUSY_LIMIT )
			return false;

		link.busy_wait = true;
		base::leave_coroutine ( link.routine );
		link.busy_wait = false;

		return !link.closed;

	}

	bool read_message ( server_connection& link, pg_message& out ) {

		if ( !need_bytes ( link, 5 ) )
			return false;

		const uint8_t* head = link.input + link.input_start;
		uint32_t size = static_cast < uint32_t > ( head [ 1 ] ) << 24 | static_cast < uint32_t > ( head [ 2 ] ) << 16 | static_cast < uint32_t > ( head [ 3 ] ) << 8 | head [ 4 ];

		if ( size < 4 || size > 0x40000000 )
			return false;

		if ( !need_bytes ( link, 1 + size ) )
			return false;

		head = link.input + link.input_start;
		out.type = head [ 0 ];
		out.body = head + 5;
		out.size = size - 4;
		link.input_start += 1 + size;

		return true;

	}

	void begin_message ( server_connection& link, uint8_t type ) {

		link.message_start = link.output.size;
		link.output.add ( static_cast < char > ( type ) );
		link.output.add ( "\0\0\0\0", 4 );

	}

	void put16 ( server_connection& link, int32_t value ) {

		char bytes [ 2 ] = { static_cast < char > ( value >> 8 ), static_cast < char > ( value ) };

		link.output.add ( bytes, 2 );

	}

	void put32 ( server_connection& link, int32_t value ) {

		char bytes [ 4 ] = { static_cast < char > ( value >> 24 ), static_cast < char > ( value >> 16 ), static_cast < char > ( value >> 8 ), static_cast < char > ( value ) };

		link.output.add ( bytes, 4 );

	}

	void put_bytes ( server_connection& link, const void* data, size_t size ) {

		link.output.add ( static_cast < const char* > ( data ), size );

	}

	void put_string ( server_connection& link, const char* value ) {

		link.output.add ( value ).add ( '\0' );

	}

	void end_message ( server_connection& link ) {

		uint32_t size = static_cast < uint32_t > ( link.output.size - link.message_start - 1 );
		char* at = link.output.data + link.message_start + 1;

		at [ 0 ] = static_cast < char > ( size >> 24 );
		at [ 1 ] = static_cast < char > ( size >> 16 );
		at [ 2 ] = static_cast < char > ( size >> 8 );
		at [ 3 ] = static_cast < char > ( size );

	}

}
