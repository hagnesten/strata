#include "test/harness/harness.hh"
#include "base/console/console.hh"

namespace test {

	test_counts counts;

	uint64_t test_random::next ( ) {

		state ^= state << 13;
		state ^= state >> 7;
		state ^= state << 17;

		return state;

	}

	uint32_t test_random::below ( uint32_t limit ) {

		return limit ? static_cast < uint32_t > ( next ( ) % limit ) : 0;

	}

	bool expect ( bool fine, const char* what ) {

		counts.checks++;

		if ( fine )
			return true;

		counts.failures++;

		base::text line;

		line.add ( "  failed  " ).add ( what ).add ( '\n' );
		base::write ( line );

		return false;

	}

	void section ( const char* name ) {

		base::text line;

		line.add ( name ).add ( '\n' );
		base::write ( line );

	}

	bool scratch_path ( char* out, size_t room, const char* name ) {

		return base::temp_path ( out, room, name );

	}

	void forget_database ( const char* path ) {

		base::text log;

		base::erase_path ( path );
		log.add ( path ).add ( "-wal" );
		base::erase_path ( log.value ( ) );

	}

	void flip_byte ( const char* path, uint64_t offset ) {

		base::file_handle file;
		uint8_t byte = 0;

		if ( !base::open_file ( file, path, false ) )
			return;

		if ( base::read_at ( file, offset, &byte, 1 ) ) {

			byte ^= 0x5A;
			base::write_at ( file, offset, &byte, 1 );

		}

		base::close_file ( file );

	}

	int32_t run_all ( ) {

		counts = { };
		base::write ( "strata self test\n" );
		pager_tests ( );

		base::text line;

		line.number ( counts.checks - counts.failures ).add ( " of " ).number ( counts.checks ).add ( " checks passed\n" );
		base::write ( line );

		return counts.failures ? 1 : 0;

	}

}
