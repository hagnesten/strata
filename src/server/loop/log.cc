#include "server/server.hh"
#include "base/file/file.hh"
#include "base/console/console.hh"

namespace server {

	static void two ( base::text& out, int64_t value ) {

		out.add ( static_cast < char > ( '0' + value / 10 % 10 ) ).add ( static_cast < char > ( '0' + value % 10 ) );

	}

	void time_stamp ( base::text& out, bool compact ) {

		int64_t milliseconds = base::os::unix_milliseconds ( );
		int64_t seconds = milliseconds / 1000;
		int64_t days = seconds / 86400;
		int64_t clock = seconds % 86400;
		int64_t year = 0;
		int64_t month = 0;
		int64_t day = 0;

		engine::moment_civil_from_days ( days, year, month, day );
		out.signed_number ( year );

		if ( !compact )
			out.add ( '-' );

		two ( out, month );

		if ( !compact )
			out.add ( '-' );

		two ( out, day );
		out.add ( compact ? '-' : ' ' );
		two ( out, clock / 3600 );

		if ( !compact )
			out.add ( ':' );

		two ( out, clock / 60 % 60 );

		if ( !compact )
			out.add ( ':' );

		two ( out, clock % 60 );

		if ( !compact ) {

			int64_t part = milliseconds % 1000;

			out.add ( '.' ).add ( static_cast < char > ( '0' + part / 100 ) );
			two ( out, part % 100 );
			out.add ( " UTC" );

		}

	}

	void server_log ( server_state& state, const char* line ) {

		base::text written;

		time_stamp ( written, false );
		written.add ( "  " ).add ( line ).add ( '\n' );

		if ( state.settings.log_path ) {

			base::file_handle file;

			if ( base::open_file ( file, state.settings.log_path, true ) ) {

				base::write_at ( file, base::file_size ( file ), written.value ( ), written.size );
				base::close_file ( file );

			}

		}

		if ( !state.settings.quiet )
			base::write ( written.value ( ) );

	}

}
