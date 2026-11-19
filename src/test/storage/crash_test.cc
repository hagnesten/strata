#include "test/harness/harness.hh"
#include "storage/btree/btree.hh"
#include "base/hash/crc32c.hh"

#define CRASH_TRANSACTIONS 8
#define CRASH_KEYS 300

namespace test {

	static void row_payload ( base::text& out, uint32_t transaction, uint32_t key ) {

		uint32_t size = ( transaction * 131 + key * 17 ) % 7 == 0 ? 1500 + key % 3000 : 10 + key % 120;

		out.clear ( );

		for ( uint32_t i = 0; i < size; i++ )
			out.add ( static_cast < char > ( 'A' + ( transaction + key + i ) % 26 ) );

	}

	static uint32_t model_digest ( const uint32_t* versions ) {

		uint32_t digest = 0x51A7A;
		base::text payload;

		for ( uint32_t key = 0; key < CRASH_KEYS; key++ ) {

			if ( !versions [ key ] )
				continue;

			int64_t rowid = key;

			row_payload ( payload, versions [ key ] - 1, key );
			digest = base::crc32c ( &rowid, 8, digest );
			digest = base::crc32c ( payload.value ( ), payload.size, digest );

		}

		return digest;

	}

	static bool tree_digest ( storage::pager& item, uint32_t& digest ) {

		digest = 0x51A7A;

		if ( !item.header.schema_root ) {

			digest = 0;

			return true;

		}

		storage::btree tree = { &item, item.header.schema_root, false };
		storage::cursor at;
		base::text payload;

		if ( !storage::cursor_first ( at, tree ) )
			return false;

		while ( at.valid ) {

			int64_t rowid = 0;

			if ( !storage::cursor_rowid ( at, rowid ) || !storage::cursor_payload ( at, payload ) )
				return false;

			digest = base::crc32c ( &rowid, 8, digest );
			digest = base::crc32c ( payload.value ( ), payload.size, digest );

			if ( !storage::cursor_next ( at ) )
				return false;

		}

		return true;

	}

	static void plan_changes ( uint32_t transaction, uint32_t* versions ) {

		test_random random = { 0xC0FFEEull + transaction * 7919ull };

		for ( uint32_t i = 0; i < 40; i++ ) {

			uint32_t key = random.below ( CRASH_KEYS );

			versions [ key ] = random.below ( 4 ) == 0 ? 0 : transaction + 1;

		}

	}

	static uint32_t run_workload ( const char* path, uint32_t* digests ) {

		storage::pager item;
		uint32_t versions [ CRASH_KEYS ] = { };
		uint32_t commits = 0;

		if ( !storage::open_pager ( item, path, 64 ) )
			return 0;

		item.wal.checkpoint_frames = 12;

		uint32_t root = 0;
		bool fine = storage::create_btree ( item, false, root );

		item.header.schema_root = root;
		fine = fine && storage::commit_pager ( item );

		if ( fine ) {

			commits++;

			if ( digests )
				digests [ commits ] = model_digest ( versions );

		}

		storage::btree tree = { &item, root, false };
		base::text payload;

		for ( uint32_t transaction = 1; transaction <= CRASH_TRANSACTIONS && fine; transaction++ ) {

			uint32_t before [ CRASH_KEYS ];

			base::copy ( before, versions, sizeof ( versions ) );
			plan_changes ( transaction, versions );

			for ( uint32_t key = 0; key < CRASH_KEYS && fine; key++ ) {

				if ( versions [ key ] == before [ key ] )
					continue;

				if ( versions [ key ] ) {

					bool existed = false;

					row_payload ( payload, versions [ key ] - 1, key );
					fine = storage::table_insert ( tree, key, reinterpret_cast < const uint8_t* > ( payload.value ( ) ), payload.size, true, existed );

				} else {

					bool gone = false;

					fine = storage::table_delete ( tree, key, gone );

				}

			}

			fine = fine && storage::commit_pager ( item );

			if ( fine ) {

				commits++;

				if ( digests )
					digests [ commits ] = model_digest ( versions );

			}

		}

		storage::close_pager ( item );

		return commits;

	}

	void crash_tests ( ) {

		section ( "crash safety" );

		char path [ FILE_PATH_ROOM ];

		if ( !expect ( scratch_path ( path, sizeof ( path ), "strata-test-crash.sdb" ), "a scratch path exists" ) )
			return;

		uint32_t digests [ CRASH_TRANSACTIONS + 2 ] = { };

		forget_database ( path );
		base::clear_faults ( );

		uint64_t before = base::faults.operations;
		uint32_t clean = run_workload ( path, digests );
		uint64_t total = base::faults.operations - before;

		expect ( clean == CRASH_TRANSACTIONS + 1, "the workload commits every transaction when nothing fails" );

		uint32_t tried = 0;
		uint32_t survived = 0;
		base::text first_problem;

		for ( uint32_t torn = 0; torn < 2; torn++ ) {

			for ( uint64_t point = 1; point <= total + 1; point++ ) {

				forget_database ( path );
				base::plan_fault ( point, torn != 0 );

				uint32_t committed = run_workload ( path, nullptr );

				base::clear_faults ( );
				tried++;

				storage::pager item;
				bool fine = storage::open_pager ( item, path );
				uint32_t digest = 0;
				storage::tree_report report;

				if ( fine && item.header.schema_root )
					fine = storage::check_btree ( item, item.header.schema_root, false, report );

				fine = fine && tree_digest ( item, digest );

				bool matches = fine && ( digest == digests [ committed ] || ( committed < CRASH_TRANSACTIONS + 1 && digest == digests [ committed + 1 ] ) );

				if ( matches ) {

					uint32_t root = item.header.schema_root;
					storage::btree tree = { &item, root, false };
					bool existed = false;
					const char* note = "written after recovery";

					if ( root )
						matches = storage::table_insert ( tree, 999999, reinterpret_cast < const uint8_t* > ( note ), 22, true, existed ) && storage::commit_pager ( item );

				}

				if ( matches )
					survived++;
				else if ( !first_problem.size ) {

					first_problem.add ( torn ? "torn write at operation " : "failure at operation " ).number ( point ).add ( " after " ).number ( committed ).add ( " commits: " ).add ( fine ? "the contents match no committed state" : item.problem );

					if ( fine && report.problem.size )
						first_problem.add ( ", " ).add ( report.problem.value ( ) );

				}

				storage::close_pager ( item );

			}

		}

		base::text line;

		line.add ( "every one of " ).number ( tried ).add ( " crash points, plain and torn, recovers to a committed state" );

		if ( first_problem.size )
			expect ( false, first_problem.value ( ) );

		expect ( survived == tried, line.value ( ) );
		forget_database ( path );

	}

}
