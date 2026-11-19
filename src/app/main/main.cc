#include "app/main/main.hh"
#include "base/console/console.hh"
#include "test/harness/harness.hh"
#include "app/shell/shell.hh"

#include <windows.h>
#include <shellapi.h>

namespace app {

	static int32_t argument_count;
	static char arguments [ MAIN_ARGUMENTS ] [ MAIN_ARGUMENT_ROOM ];

	static void read_arguments ( ) {

		int32_t count = 0;
		wchar_t** wide = CommandLineToArgvW ( GetCommandLineW ( ), &count );

		argument_count = 0;

		for ( int32_t i = 0; wide && i < count && i < MAIN_ARGUMENTS; i++ )
			if ( WideCharToMultiByte ( CP_UTF8, 0, wide [ i ], -1, arguments [ argument_count ], MAIN_ARGUMENT_ROOM, nullptr, nullptr ) > 0 )
				argument_count++;

		if ( wide )
			LocalFree ( wide );

	}

	int32_t run ( ) {

		read_arguments ( );

		if ( argument_count > 1 && base::same ( arguments [ 1 ], "--test" ) )
			return test::run_all ( argument_count > 2 ? arguments [ 2 ] : nullptr );

		if ( argument_count == 2 && arguments [ 1 ] [ 0 ] != '-' )
			return run_shell ( arguments [ 1 ] );

		if ( argument_count == 3 && arguments [ 1 ] [ 0 ] != '-' )
			return run_sql ( arguments [ 1 ], arguments [ 2 ] );

		base::write ( "strata " STRATA_VERSION "\n" );
		base::write ( "  strata <file>            open a database in the SQL shell\n" );
		base::write ( "  strata <file> \"<sql>\"    run SQL against a database and print the rows\n" );
		base::write ( "  strata --test [part]     run the self test (pager, btree, crash, parser, engine)\n" );

		return 0;

	}

}
