#include "test/engine/checks.hh"

#define SAVEPOINT_KEYS 120
#define SAVEPOINT_DEPTH 8
#define SAVEPOINT_STEPS 3000

namespace test {

	static void savepoint_basics ( engine::database& db ) {

		run ( db, "CREATE TABLE marks ( k INTEGER PRIMARY KEY, v TEXT )" );
		run ( db, "SAVEPOINT a" );
		run ( db, "INSERT INTO marks VALUES ( 1, 'one' )" );
		run ( db, "SAVEPOINT b" );
		run ( db, "INSERT INTO marks VALUES ( 2, 'two' )" );
		rows ( db, "SELECT count ( * ) FROM marks", "2" );
		run ( db, "ROLLBACK TO b" );
		rows ( db, "SELECT group_concat ( k ) FROM marks", "1" );
		run ( db, "INSERT INTO marks VALUES ( 3, 'three' )" );
		run ( db, "ROLLBACK TRANSACTION TO SAVEPOINT b" );
		rows ( db, "SELECT group_concat ( k ) FROM marks", "1" );
		run ( db, "RELEASE b" );
		fails ( db, "ROLLBACK TO b", "no such savepoint: b" );
		run ( db, "RELEASE SAVEPOINT a" );
		fails ( db, "COMMIT", "no open transaction" );
		rows ( db, "SELECT group_concat ( k ) FROM marks", "1" );
		run ( db, "BEGIN" );
		run ( db, "INSERT INTO marks VALUES ( 4, 'four' )" );
		run ( db, "SAVEPOINT s" );
		run ( db, "CREATE TABLE scratch_table ( x )" );
		run ( db, "INSERT INTO scratch_table VALUES ( 1 )" );
		run ( db, "ROLLBACK TO s" );
		fails ( db, "SELECT * FROM scratch_table", "no such table" );
		run ( db, "RELEASE s" );
		rows ( db, "SELECT count ( * ) FROM marks", "2" );
		run ( db, "ROLLBACK" );
		rows ( db, "SELECT count ( * ) FROM marks", "1" );
		run ( db, "SAVEPOINT x; INSERT INTO marks VALUES ( 10, 'ten' ); SAVEPOINT x; INSERT INTO marks VALUES ( 11, 'eleven' ); ROLLBACK TO x" );
		rows ( db, "SELECT group_concat ( k ) FROM marks", "1,10" );
		run ( db, "RELEASE x" );
		rows ( db, "SELECT count ( * ) FROM marks", "2" );
		run ( db, "RELEASE x" );
		fails ( db, "RELEASE x", "no such savepoint" );
		run ( db, "SAVEPOINT f; INSERT INTO marks VALUES ( 20, 'twenty' )" );
		fails ( db, "INSERT INTO marks VALUES ( 21, 'a' ), ( 20, 'again' )", "UNIQUE" );
		rows ( db, "SELECT group_concat ( k ) FROM marks", "1,10,20" );
		run ( db, "ROLLBACK TO f" );
		rows ( db, "SELECT group_concat ( k ) FROM marks", "1,10" );
		run ( db, "ROLLBACK" );
		rows ( db, "SELECT group_concat ( k ) FROM marks", "1,10" );

	}

	static void describe_model ( const int32_t* model, base::text& out ) {

		bool first = true;

		out.clear ( );

		for ( uint32_t k = 0; k < SAVEPOINT_KEYS; k++ ) {

			if ( model [ k ] < 0 )
				continue;

			if ( !first )
				out.add ( '\n' );

			out.number ( k ).add ( '|' ).number ( static_cast < uint32_t > ( model [ k ] ) );
			first = false;

		}

	}

	static void savepoint_fuzz ( engine::database& db, const char* path ) {

		int32_t* model = base::make < int32_t > ( SAVEPOINT_KEYS * ( SAVEPOINT_DEPTH + 2 ) );

		if ( !expect ( model != nullptr, "the savepoint model has memory" ) )
			return;

		int32_t* committed = model + SAVEPOINT_KEYS;
		int32_t* saved = committed + SAVEPOINT_KEYS;
		test_random random = { 0x8CB92BA72F3D8DD7ull };
		uint32_t depth = 0;
		uint32_t next_id = 0;
		uint32_t mismatches = 0;
		uint32_t reopens = 0;
		bool in_transaction = false;
		bool opened = false;
		bool fine = true;
		base::text sql;
		base::text wanted;

		for ( uint32_t k = 0; k < SAVEPOINT_KEYS; k++ )
			model [ k ] = committed [ k ] = -1;

		run ( db, "CREATE TABLE spz ( k INTEGER PRIMARY KEY, id INT, pad BLOB )" );
		run ( db, "CREATE INDEX spz_id ON spz ( id )" );

		for ( uint32_t step = 0; step < SAVEPOINT_STEPS && fine; step++ ) {

			uint32_t action = random.below ( 100 );
			uint32_t k = random.below ( SAVEPOINT_KEYS );
			bool should_fail = false;

			sql.clear ( );

			if ( action < 40 ) {

				sql.add ( "INSERT OR REPLACE INTO spz VALUES ( " ).number ( k ).add ( ", " ).number ( next_id ).add ( ", randomblob ( " ).number ( 100 + random.below ( 400 ) ).add ( " ) )" );
				model [ k ] = static_cast < int32_t > ( next_id++ );

			} else if ( action < 55 ) {

				sql.add ( "DELETE FROM spz WHERE k = " ).number ( k );
				model [ k ] = -1;

			} else if ( action < 65 ) {

				sql.add ( "UPDATE spz SET id = " ).number ( next_id ).add ( ", pad = randomblob ( 250 ) WHERE k = " ).number ( k );

				if ( model [ k ] >= 0 )
					model [ k ] = static_cast < int32_t > ( next_id );

				next_id++;

			} else if ( action < 70 ) {

				uint32_t absent = SAVEPOINT_KEYS;
				uint32_t present = SAVEPOINT_KEYS;

				for ( uint32_t probe = 0; probe < SAVEPOINT_KEYS; probe++ ) {

					uint32_t at = ( k + probe ) % SAVEPOINT_KEYS;

					if ( model [ at ] < 0 && absent == SAVEPOINT_KEYS )
						absent = at;

					if ( model [ at ] >= 0 && present == SAVEPOINT_KEYS )
						present = at;

				}

				if ( absent == SAVEPOINT_KEYS || present == SAVEPOINT_KEYS )
					continue;

				sql.add ( "INSERT INTO spz VALUES ( " ).number ( absent ).add ( ", 0, randomblob ( 300 ) ), ( " ).number ( present ).add ( ", 0, zeroblob ( 1 ) )" );
				should_fail = true;

			} else if ( action < 80 ) {

				if ( depth == SAVEPOINT_DEPTH )
					continue;

				sql.add ( "SAVEPOINT s" ).number ( depth );
				base::copy ( saved + depth * SAVEPOINT_KEYS, model, sizeof ( int32_t ) * SAVEPOINT_KEYS );

				if ( !in_transaction )
					opened = true;

				in_transaction = true;
				depth++;

			} else if ( action < 87 ) {

				if ( !depth )
					continue;

				uint32_t target = random.below ( depth );

				sql.add ( "ROLLBACK TO s" ).number ( target );
				base::copy ( model, saved + target * SAVEPOINT_KEYS, sizeof ( int32_t ) * SAVEPOINT_KEYS );
				depth = target + 1;

			} else if ( action < 93 ) {

				if ( !depth )
					continue;

				uint32_t target = random.below ( depth );

				sql.add ( "RELEASE s" ).number ( target );
				depth = target;

				if ( !target && opened ) {

					in_transaction = false;
					opened = false;

				}

			} else if ( action < 96 ) {

				if ( in_transaction )
					continue;

				sql.add ( "BEGIN" );
				in_transaction = true;

			} else if ( action < 98 ) {

				if ( !in_transaction )
					continue;

				sql.add ( "COMMIT" );
				in_transaction = false;
				opened = false;
				depth = 0;

			} else {

				if ( !in_transaction )
					continue;

				sql.add ( "ROLLBACK" );
				base::copy ( model, committed, sizeof ( int32_t ) * SAVEPOINT_KEYS );
				in_transaction = false;
				opened = false;
				depth = 0;

			}

			bool ran = engine::execute ( db, sql.value ( ), nullptr, 0, nullptr );

			if ( ran == should_fail ) {

				base::text line;

				line.add ( sql.value ( ) ).add ( should_fail ? ": ran but should fail" : ": " ).add ( db.problem.value ( ) );
				expect ( false, line.value ( ) );
				fine = false;

				break;

			}

			if ( !in_transaction )
				base::copy ( committed, model, sizeof ( int32_t ) * SAVEPOINT_KEYS );

			result_capture capture;

			describe_model ( model, wanted );

			if ( !query ( db, "SELECT k, id FROM spz ORDER BY k", capture ) || !base::same ( capture.out.value ( ), wanted.value ( ) ) ) {

				if ( mismatches++ < 3 ) {

					base::text line;

					line.add ( "after " ).add ( sql.value ( ) ).add ( " at step " ).number ( step ).add ( "\n          got:    " ).add ( capture.out.value ( ) ).add ( "\n          wanted: " ).add ( wanted.value ( ) );
					expect ( false, line.value ( ) );

				}

				fine = false;

			}

			if ( fine && !in_transaction && random.below ( 300 ) == 0 ) {

				engine::close_database ( db );
				fine = engine::open_database ( db, path );
				reopens++;

				if ( !fine )
					expect ( false, db.problem.value ( ) );

			}

		}

		expect ( fine && !mismatches, "random savepoints, rollbacks and releases agree with the model" );

		if ( in_transaction )
			engine::execute ( db, "ROLLBACK", nullptr, 0, nullptr );

		uint64_t live = 0;

		for ( uint32_t k = 0; k < SAVEPOINT_KEYS; k++ )
			live += committed [ k ] >= 0;

		for ( uint32_t i = 0; i < db.schema->index_count; i++ ) {

			engine::index_info& index = db.schema->indexes [ i ];

			if ( !base::same ( index.table, "spz" ) )
				continue;

			storage::tree_report report;
			bool intact = storage::check_btree ( db.pages, index.root, true, report );

			expect ( intact && report.entries == live, "the savepoint fuzz index holds one entry per committed row" );

		}

		base::text summary;

		summary.number ( reopens ).add ( " reopens kept every committed savepoint change" );
		expect ( fine && reopens > 0, summary.value ( ) );
		base::release ( model );

	}

	void savepoint_tests ( engine::database& db, const char* path ) {

		savepoint_basics ( db );
		savepoint_fuzz ( db, path );

	}

}