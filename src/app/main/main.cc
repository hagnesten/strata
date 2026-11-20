#include "app/main/main.hh"
#include "base/console/console.hh"
#include "base/platform/os.hh"
#include "test/harness/harness.hh"
#include "app/shell/shell.hh"
#include "server/server.hh"
#include "app/backup/backup.hh"

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
		settings.backup_keep = 14;

		for ( int32_t i = 3; i < count; i++ ) {

			uint64_t number = 0;

			if ( base::same ( arguments [ i ], "--trust" ) )
				settings.trust = true;
			else if ( base::same ( arguments [ i ], "--quiet" ) )
				settings.quiet = true;
			else if ( base::same ( arguments [ i ], "--host" ) && i + 1 < count )
				settings.host = arguments [ ++i ];
			else if ( base::same ( arguments [ i ], "--backup-folder" ) && i + 1 < count )
				settings.backup_folder = arguments [ ++i ];
			else if ( base::same ( arguments [ i ], "--log" ) && i + 1 < count )
				settings.log_path = arguments [ ++i ];
			else if ( base::same ( arguments [ i ], "--tls" ) )
				settings.tls = true;
			else if ( base::same ( arguments [ i ], "--tls-required" ) )
				settings.tls = settings.tls_required = true;
			else if ( base::same ( arguments [ i ], "--tls-cert" ) && i + 1 < count ) {

				settings.tls = true;
				settings.tls_certificate = arguments [ ++i ];

			} else if ( base::same ( arguments [ i ], "--tls-key" ) && i + 1 < count )
				settings.tls_key = arguments [ ++i ];
			else if ( ( base::same ( arguments [ i ], "--backup-every" ) || base::same ( arguments [ i ], "--backup-keep" ) || base::same ( arguments [ i ], "--slow" ) ) && i + 1 < count && base::read_number ( arguments [ i + 1 ], number ) && number < 100000000 ) {

				if ( base::same ( arguments [ i ], "--backup-every" ) )
					settings.backup_minutes = static_cast < uint32_t > ( number );
				else if ( base::same ( arguments [ i ], "--backup-keep" ) )
					settings.backup_keep = static_cast < uint32_t > ( number );
				else
					settings.slow_milliseconds = static_cast < uint32_t > ( number );

				i++;

			}
			else if ( base::same ( arguments [ i ], "--port" ) && i + 1 < count && base::read_number ( arguments [ i + 1 ], number ) && number && number < 65536 ) {

				settings.port = static_cast < uint16_t > ( number );
				i++;

			} else {

				base::write_error ( "strata --serve <file> [--port 5432] [--host 0.0.0.0] [--trust] [--backup-folder <dir> --backup-every <minutes> --backup-keep <count>] [--slow <ms>] [--log <file>] [--tls] [--tls-required] [--tls-cert <pem> --tls-key <pem>]\n" );

				return 2;

			}

		}

		return server::run_server ( settings );

	}

	int32_t run ( int32_t count, char** arguments, bool windowed ) {

		bool handled = false;
		int32_t code = run_tools ( count, arguments, windowed, handled );

		if ( handled )
			return code;

		if ( named ( count, arguments, 1, "--serve" ) && count > 2 )
			return run_serve ( count, arguments );

		if ( named ( count, arguments, 1, "--user" ) && count > 3 )
			return server::manage_user ( arguments [ 2 ], arguments [ 3 ], count > 4 ? arguments [ 4 ] : nullptr, count > 5 && base::same ( arguments [ 5 ], "--limited" ) );

		if ( named ( count, arguments, 1, "--check" ) && count > 2 )
			return run_check ( arguments [ 2 ] );

		if ( named ( count, arguments, 1, "--backup" ) && count > 3 )
			return run_backup ( arguments [ 2 ], arguments [ 3 ] );

		if ( named ( count, arguments, 1, "--restore" ) && count > 3 )
			return run_restore ( arguments [ 2 ], arguments [ 3 ] );

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
		base::write ( "  strata --backup <file> <copy>     make a checked copy, safe while the server runs\n" );
		base::write ( "  strata --restore <copy> <file>    check a backup and restore it to a new path\n" );
		base::write ( "  strata --serve <file>    serve a database to Postgres clients (--port, --host, --trust)\n" );
		base::write ( "  strata --user <file> <name> [password] [--limited]   add, change or remove a server user\n" );

		if ( STRATA_WINDOWS ) {

			base::write ( "  strata --studio [file]   open Strata Studio\n" );
			base::write ( "  strata --capture <dir>   render every studio view to PNG files\n" );
			base::write ( "  strata --bench [rows]    race Strata against SQLite (--sqlite <dll>, --out <file.md>)\n" );

		}

		return 0;

	}

}
