#include "app/main/main.hh"
#include "base/console/console.hh"
#include "base/platform/os.hh"
#include "test/harness/harness.hh"
#include "app/shell/shell.hh"
#include "server/server.hh"
#include "engine/check/check.hh"

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

	static int32_t run_serve ( int32_t count, char** arguments ) {

		server::server_settings settings = { };

		settings.path = arguments [ 2 ];
		settings.host = "0.0.0.0";
		settings.port = 5432;

		for ( int32_t i = 3; i < count; i++ ) {

			uint64_t number = 0;

			if ( base::same ( arguments [ i ], "--trust" ) )
				settings.trust = true;
			else if ( base::same ( arguments [ i ], "--quiet" ) )
				settings.quiet = true;
			else if ( base::same ( arguments [ i ], "--host" ) && i + 1 < count )
				settings.host = arguments [ ++i ];
			else if ( base::same ( arguments [ i ], "--port" ) && i + 1 < count && base::read_number ( arguments [ i + 1 ], number ) && number && number < 65536 ) {

				settings.port = static_cast < uint16_t > ( number );
				i++;

			} else {

				base::write_error ( "strata --serve <file> [--port 5432] [--host 0.0.0.0] [--trust]\n" );

				return 2;

			}

		}

		return server::run_server ( settings );

	}

	static int32_t run_check ( const char* path ) {

		if ( !base::path_exists ( path ) ) {

			base::write_error ( "there is no database at that path\n" );

			return 2;

		}

		engine::database* db = base::make < engine::database > ( 1 );
		engine::check_report report = { };

		if ( !db || !engine::open_database ( *db, path ) ) {

			base::write_error ( "the database could not be opened" );

			if ( db && db->problem.size )
				base::write_error ( ": " ), base::write_error ( db->problem.value ( ) );

			base::write_error ( "\n" );

			if ( db ) {

				db->problem.~text ( );
				base::release ( db );

			}

			return 2;

		}

		bool fine = engine::check_database ( *db, report );
		base::text line;

		line.add ( "checked " ).number ( report.pages ).add ( " pages, " ).number ( report.trees ).add ( " trees, " ).number ( report.rows ).add ( " rows, " ).number ( report.entries ).add ( " index entries, " ).number ( report.free_pages ).add ( " free pages\n" );
		base::write ( line.value ( ) );

		for ( uint32_t i = 0; i < report.problem_count; i++ ) {

			base::write ( "  problem: " );
			base::write ( report.problems [ i ] );
			base::write ( "\n" );

		}

		bool clean = fine && !report.problem_count;

		base::write ( clean ? "ok\n" : "the file has problems\n" );
		engine::forget_report ( report );
		engine::close_database ( *db );
		db->problem.~text ( );
		base::release ( db );

		return clean ? 0 : 1;

	}

	int32_t run ( int32_t count, char** arguments, bool windowed ) {

		bool handled = false;
		int32_t code = run_tools ( count, arguments, windowed, handled );

		if ( handled )
			return code;

		if ( named ( count, arguments, 1, "--serve" ) && count > 2 )
			return run_serve ( count, arguments );

		if ( named ( count, arguments, 1, "--user" ) && count > 3 )
			return server::manage_user ( arguments [ 2 ], arguments [ 3 ], count > 4 ? arguments [ 4 ] : nullptr );

		if ( named ( count, arguments, 1, "--check" ) && count > 2 )
			return run_check ( arguments [ 2 ] );

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
		base::write ( "  strata --check <file>    check every page, tree, row and index entry of a database\n" );
		base::write ( "  strata --serve <file>    serve a database to Postgres clients (--port, --host, --trust)\n" );
		base::write ( "  strata --user <file> <name> [password]   add, change or remove a server user\n" );

		if ( STRATA_WINDOWS ) {

			base::write ( "  strata --studio [file]   open Strata Studio\n" );
			base::write ( "  strata --capture <dir>   render every studio view to PNG files\n" );
			base::write ( "  strata --bench [rows]    race Strata against SQLite (--sqlite <dll>, --out <file.md>)\n" );

		}

		return 0;

	}

}
