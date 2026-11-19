#include "app/main/main.hh"
#include "base/console/console.hh"

namespace app {

	int32_t run ( ) {

		base::write ( "strata " STRATA_VERSION "\n" );
		base::write ( "a single-file SQL database, nothing to open yet\n" );

		return 0;

	}

}
