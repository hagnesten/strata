#include "bench/postgres/wire.hh"
#include "base/platform/os.hh"
#include "base/runtime/memory.hh"

namespace bench {

	static bool grow ( uint8_t*& data, size_t& room, size_t used, size_t wanted ) {

		if ( wanted <= room )
			return true;

		size_t next = room ? room : 4096;

		while ( next < wanted )
			next *= 2;

		uint8_t* made = static_cast < uint8_t* > ( base::allocate ( next ) );

		if ( !made )
			return false;

		if ( used )
			base::copy ( made, data, used );

		base::release ( data );
		data = made;
		room = next;

		return true;

	}

	static void put ( wire_link& link, const void* data, size_t size ) {

		if ( link.output_failed )
			return;

		if ( !grow ( link.output, link.output_room, link.output_size, link.output_size + size ) ) {

			link.output_failed = true;

			return;

		}

		base::copy ( link.output + link.output_size, data, size );
		link.output_size += size;

	}

	void wire_begin ( wire_link& link, uint8_t type ) {

		uint8_t header [ 5 ] = { type, 0, 0, 0, 0 };

		put ( link, type ? header : header + 1, type ? 5 : 4 );
		link.message_start = link.output_size - 4;

	}

	void wire_int16 ( wire_link& link, int32_t value ) {

		uint8_t bytes [ 2 ] = { static_cast < uint8_t > ( value >> 8 ), static_cast < uint8_t > ( value ) };

		put ( link, bytes, 2 );

	}

	void wire_int32 ( wire_link& link, int32_t value ) {

		uint32_t bits = static_cast < uint32_t > ( value );
		uint8_t bytes [ 4 ] = { static_cast < uint8_t > ( bits >> 24 ), static_cast < uint8_t > ( bits >> 16 ), static_cast < uint8_t > ( bits >> 8 ), static_cast < uint8_t > ( bits ) };

		put ( link, bytes, 4 );

	}

	void wire_bytes ( wire_link& link, const void* data, size_t size ) {

		if ( size )
			put ( link, data, size );

	}

	void wire_string ( wire_link& link, const char* value ) {

		put ( link, value, base::length ( value ) + 1 );

	}

	void wire_end ( wire_link& link ) {

		if ( link.output_failed )
			return;

		uint32_t size = static_cast < uint32_t > ( link.output_size - link.message_start );
		uint8_t* at = link.output + link.message_start;

		at [ 0 ] = static_cast < uint8_t > ( size >> 24 );
		at [ 1 ] = static_cast < uint8_t > ( size >> 16 );
		at [ 2 ] = static_cast < uint8_t > ( size >> 8 );
		at [ 3 ] = static_cast < uint8_t > ( size );

	}

	void wire_fail ( wire_link& link, const char* what ) {

		link.problem.clear ( );
		link.problem.add ( what );

	}

	bool wire_flush ( wire_link& link ) {

		if ( link.output_failed ) {

			link.output_failed = false;
			link.output_size = 0;
			wire_fail ( link, "out of memory for a Postgres message" );

			return false;

		}

		bool fine = base::os::send_all ( link.socket, link.output, link.output_size );

		link.output_size = 0;

		if ( !fine ) {

			link.broken = true;
			wire_fail ( link, "the connection to Postgres was lost while sending" );

		}

		return fine;

	}

	static bool fill_input ( wire_link& link, size_t wanted ) {

		if ( link.input_end - link.input_start >= wanted )
			return true;

		if ( link.input_start ) {

			size_t kept = link.input_end - link.input_start;

			if ( kept )
				base::move ( link.input, link.input + link.input_start, kept );

			link.input_start = 0;
			link.input_end = kept;

		}

		if ( !grow ( link.input, link.input_room, link.input_end, wanted > WIRE_INPUT_ROOM ? wanted : WIRE_INPUT_ROOM ) ) {

			wire_fail ( link, "out of memory for a Postgres reply" );

			return false;

		}

		while ( link.input_end < wanted ) {

			size_t got = 0;

			if ( !base::os::receive_some ( link.socket, link.input + link.input_end, link.input_room - link.input_end, got ) ) {

				link.broken = true;
				wire_fail ( link, "the connection to Postgres was lost while reading" );

				return false;

			}

			link.input_end += got;

		}

		return true;

	}

	bool wire_next ( wire_link& link, wire_message& out ) {

		if ( link.broken || !fill_input ( link, 5 ) )
			return false;

		uint32_t size = wire_read32 ( link.input + link.input_start + 1 );

		if ( size < 4 ) {

			link.broken = true;
			wire_fail ( link, "Postgres sent a malformed message" );

			return false;

		}

		if ( !fill_input ( link, size + 1 ) )
			return false;

		out.type = link.input [ link.input_start ];
		out.body = link.input + link.input_start + 5;
		out.size = size - 4;
		link.input_start += size + 1;

		return true;

	}

	void wire_note_error ( wire_link& link, const wire_message& message ) {

		const char* text = nullptr;
		const char* code = nullptr;
		uint32_t at = 0;

		while ( at < message.size && message.body [ at ] ) {

			uint8_t field = message.body [ at++ ];
			const char* value = reinterpret_cast < const char* > ( message.body + at );

			while ( at < message.size && message.body [ at ] )
				at++;

			at++;

			if ( field == 'M' )
				text = value;
			else if ( field == 'C' )
				code = value;

		}

		link.problem.clear ( );
		link.problem.add ( "Postgres: " ).add ( text ? text : "an error without a message" );

		if ( code )
			link.problem.add ( " (" ).add ( code ).add ( ')' );

	}

	bool wire_settle ( wire_link& link ) {

		bool fine = true;
		wire_message message;

		for ( ;; ) {

			if ( !wire_next ( link, message ) )
				return false;

			if ( message.type == 'E' ) {

				wire_note_error ( link, message );
				fine = false;

			} else if ( message.type == 'Z' ) {

				link.status = message.size ? message.body [ 0 ] : 'I';

				return fine;

			}

		}

	}

	bool wire_simple ( wire_link& link, const char* sql ) {

		wire_begin ( link, 'Q' );
		wire_string ( link, sql );
		wire_end ( link );

		return wire_flush ( link ) && wire_settle ( link );

	}

	void wire_close ( wire_link& link ) {

		if ( link.socket ) {

			if ( !link.broken ) {

				wire_begin ( link, 'X' );
				wire_end ( link );
				wire_flush ( link );

			}

			base::os::close_socket ( link.socket );

		}

		base::release ( link.input );
		base::release ( link.output );
		link.socket = nullptr;
		link.input = nullptr;
		link.output = nullptr;
		link.input_room = 0;
		link.output_room = 0;
		link.input_start = 0;
		link.input_end = 0;
		link.output_size = 0;

	}

}
