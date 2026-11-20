#include "server/server.hh"

namespace server {

	static server_state* watched = nullptr;

	static void quoted ( base::text& out, const char* value ) {

		if ( !value || !*value ) {

			out.add ( "NULL" );

			return;

		}

		out.add ( '\'' );

		for ( const char* at = value; *at; at++ ) {

			if ( *at == '\'' )
				out.add ( '\'' );

			out.add ( *at );

		}

		out.add ( '\'' );

	}

	static void write_activity ( base::text& rows, uint32_t& count ) {

		if ( !watched )
			return;

		for ( uint32_t i = 0; i < watched->connection_count; i++ ) {

			server_connection& link = *watched->connections [ i ];

			if ( !link.db || link.closed )
				continue;

			bool open = strata_in_transaction ( link.db );
			const char* state = link.active ? "active" : link.failed ? "idle in transaction (aborted)" : open ? "idle in transaction" : "idle";

			rows.add ( count++ ? ", ( " : "( " ).number ( link.id ).add ( ", " );
			quoted ( rows, link.user );
			rows.add ( ", " );
			quoted ( rows, link.application );
			rows.add ( ", " );
			quoted ( rows, link.started );
			rows.add ( ", " );
			quoted ( rows, link.query_start );
			rows.add ( ", " );
			quoted ( rows, state );
			rows.add ( ", " );
			quoted ( rows, link.query );
			rows.add ( link.lock_wait ? ", 'Lock', 'transactionid' )" : ", NULL, NULL )" );

		}

	}

	static int32_t terminate_session ( uint32_t session, int64_t target ) {

		if ( !watched )
			return -1;

		server_connection* caller = nullptr;

		for ( uint32_t i = 0; i < watched->connection_count; i++ )
			if ( watched->connections [ i ]->id == session )
				caller = watched->connections [ i ];

		if ( !caller || caller->limited )
			return -1;

		int32_t ended = 0;

		for ( uint32_t i = 0; i < watched->connection_count; i++ ) {

			server_connection& link = *watched->connections [ i ];

			if ( static_cast < int64_t > ( link.id ) != target || link.closed )
				continue;

			link.terminate = true;
			ended = 1;

			base::text line;

			line.add ( "session " ).number ( link.id ).add ( " of " ).add ( link.user ).add ( " was ended by " ).add ( caller->user );
			server_log ( *watched, line.value ( ) );

		}

		return ended;

	}

	void watch_activity ( server_state& state ) {

		watched = &state;
		engine::activity_hook = write_activity;
		engine::terminate_hook = terminate_session;

	}

	void begin_activity ( server_connection& link, const char* text ) {

		base::copy_text ( link.query, sizeof ( link.query ), text );
		link.query_start [ 0 ] = 0;

		base::text stamp;

		time_stamp ( stamp, false );

		if ( stamp.size > 4 )
			stamp.size -= 4;

		base::copy_span ( link.query_start, sizeof ( link.query_start ), stamp.value ( ), stamp.size );
		link.active = true;

	}

	void end_activity ( server_connection& link, uint64_t started ) {

		link.active = false;

		uint32_t slow = link.owner->settings.slow_milliseconds;

		if ( !slow )
			return;

		uint64_t elapsed = ( base::os::ticks ( ) - started ) * 1000 / base::os::tick_rate ( );

		if ( elapsed < slow )
			return;

		base::text line;

		line.add ( "slow statement, " ).number ( elapsed ).add ( " ms, " ).add ( link.user ).add ( ": " ).add ( link.query );
		server_log ( *link.owner, line.value ( ) );

	}

}
