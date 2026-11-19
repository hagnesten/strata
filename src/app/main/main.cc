#include "app/main/main.hh"
#include "base/console/console.hh"
#include "base/platform/os.hh"
#include "test/harness/harness.hh"
#include "app/shell/shell.hh"

#if STRATA_WINDOWS
#include "studio/app/studio.hh"
#include "bench/harness/bench.hh"
#endif

namespace app {

	static bool named ( int32_t count, char** arguments, int32_t at, const char* name ) {

		return count > at && base::same ( arguments [ at ], name );

	}

#if !STRATA_WINDOWS
	static int32_t windows_only ( const char* what ) {

		base::write_error ( what );
		base::write_error ( " needs Windows for now\n" );

		return 2;

	}
#endif

	static int32_t run_tools ( int32_t count, char** arguments, bool windowed, bool& handled ) {

		handled = true;

#if STRATA_WINDOWS
		if ( windowed )
			return studio::run_studio ( count > 1 ? arguments [ 1 ] : nullptr );

		if ( named ( count, arguments, 1, "--studio" ) )
			return studio::run_studio ( count > 2 ? arguments [ 2 ] : nullptr );

		if ( named ( count, arguments, 1, "--capture" ) && count > 2 )
			return studio::capture_studio ( arguments [ 2 ] );

		if ( named ( count, arguments, 1, "--make-studio" ) )
			return studio::make_studio_copy ( ) ? 0 : 1;

		if ( named ( count, arguments, 1, "--bench" ) )
			return bench::run_bench ( count - 2, arguments + 2 );
#else
		( void ) windowed;

		if ( named ( count, arguments, 1, "--studio" ) || named ( count, arguments, 1, "--capture" ) || named ( count, arguments, 1, "--make-studio" ) )
			return windows_only ( "Strata Studio" );

		if ( named ( count, arguments, 1, "--bench" ) )
			return windows_only ( "the SQLite benchmark" );
#endif

		handled = false;

		return 0;

	}

	int32_t run ( int32_t count, char** arguments, bool windowed ) {

		bool handled = false;
		int32_t code = run_tools ( count, arguments, windowed, handled );

		if ( handled )
			return code;

		if ( named ( count, arguments, 1, "--test" ) )
			return test::run_all ( count > 2 ? arguments [ 2 ] : nullptr );

		if ( count == 2 && arguments [ 1 ] [ 0 ] != '-' )
			return run_shell ( arguments [ 1 ] );

		if ( count == 3 && arguments [ 1 ] [ 0 ] != '-' )
			return run_sql ( arguments [ 1 ], arguments [ 2 ] );

		base::write ( "strata " STRATA_VERSION "\n" );
		base::write ( "  strata <file>            open a database in the SQL shell\n" );
		base::write ( "  strata <file> \"<sql>\"    run SQL against a database and print the rows\n" );
		base::write ( "  strata --test [part]     run the self test (pager, btree, crash, parser, engine)\n" );

		if ( STRATA_WINDOWS ) {

			base::write ( "  strata --studio [file]   open Strata Studio\n" );
			base::write ( "  strata --capture <dir>   render every studio view to PNG files\n" );
			base::write ( "  strata --bench [rows]    race Strata against SQLite (--sqlite <dll>, --out <file.md>)\n" );

		}

		return 0;

	}

}
