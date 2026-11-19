#include "test/engine/checks.hh"

namespace test {

	static bool trees_intact ( engine::database& db ) {

		for ( uint32_t t = 0; t < db.schema->table_count; t++ ) {

			storage::tree_report report;

			if ( !storage::check_btree ( db.pages, db.schema->tables [ t ].root, false, report ) )
				return false;

		}

		for ( uint32_t i = 0; i < db.schema->index_count; i++ ) {

			storage::tree_report report;

			if ( !storage::check_btree ( db.pages, db.schema->indexes [ i ].root, true, report ) )
				return false;

		}

		return true;

	}

	void vacuum_tests ( engine::database& db, const char* path ) {

		run ( db, "CREATE TABLE bulky ( id INTEGER PRIMARY KEY, tag TEXT, body BLOB )" );
		run ( db, "CREATE INDEX bulky_tag ON bulky ( tag )" );
		run ( db, "WITH RECURSIVE n ( i ) AS ( SELECT 1 UNION ALL SELECT i + 1 FROM n WHERE i < 3000 ) INSERT INTO bulky ( tag, body ) SELECT 'tag' || ( i % 50 ), randomblob ( 300 + i % 2000 ) FROM n" );
		run ( db, "DELETE FROM bulky WHERE id % 10 <> 0" );
		rows ( db, "SELECT count ( * ), sum ( id ) FROM bulky", "300|451500" );

		uint32_t before = db.pages.header.page_count;
		uint32_t free_before = db.pages.header.free_count;

		fails ( db, "BEGIN; VACUUM", "inside a transaction" );
		run ( db, "ROLLBACK" );
		run ( db, "VACUUM" );

		uint32_t after = db.pages.header.page_count;
		base::text line;

		line.add ( "VACUUM shrank the file from " ).number ( before ).add ( " to " ).number ( after ).add ( " pages and emptied " ).number ( free_before ).add ( " free pages" );
		expect ( free_before > 100 && after < before - free_before / 2 && db.pages.header.free_count == 0, line.value ( ) );
		expect ( trees_intact ( db ), "every tree is intact after VACUUM" );
		rows ( db, "SELECT count ( * ), sum ( id ), sum ( length ( body ) ) > 300000 FROM bulky", "300|451500|1" );
		rows ( db, "SELECT count ( * ) FROM bulky WHERE tag = 'tag10'", "60" );
		plan_has ( db, "EXPLAIN QUERY PLAN SELECT id FROM bulky WHERE tag = 'tag10'", "USING COVERING INDEX bulky_tag" );
		plan_has ( db, "EXPLAIN QUERY PLAN SELECT body FROM bulky WHERE tag = 'tag10'", "USING INDEX bulky_tag" );
		run ( db, "INSERT INTO bulky ( tag, body ) VALUES ( 'late', x'01' )" );
		rows ( db, "SELECT id FROM bulky WHERE tag = 'late'", "3001" );
		rows ( db, "SELECT count ( * ) FROM people", "4" );

		engine::close_database ( db );
		expect ( engine::open_database ( db, path ), "the vacuumed database opens again" );
		rows ( db, "SELECT count ( * ), max ( id ) FROM bulky", "301|3001" );
		expect ( trees_intact ( db ), "every tree is intact after reopening" );

		char copy [ FILE_PATH_ROOM ];

		if ( !expect ( scratch_path ( copy, sizeof ( copy ), "strata-test-vacuum-copy.sdb" ), "a scratch path for the copy exists" ) )
			return;

		forget_database ( copy );

		base::text into;

		into.add ( "VACUUM INTO '" ).add ( copy ).add ( "'" );
		run ( db, into.value ( ) );
		fails ( db, into.value ( ), "already exists" );

		engine::database* other = base::make < engine::database > ( 1 );

		if ( expect ( other && engine::open_database ( *other, copy ), "the VACUUM INTO copy opens" ) ) {

			rows ( *other, "SELECT count ( * ), max ( id ) FROM bulky", "301|3001" );
			rows ( *other, "SELECT count ( * ) FROM bulky WHERE tag = 'tag20'", "60" );
			expect ( other->pages.header.free_count == 0 && trees_intact ( *other ), "the copy is compact and intact" );
			engine::close_database ( *other );

		}

		if ( other ) {

			other->problem.~text ( );
			base::release ( other );

		}

		forget_database ( copy );
		run ( db, "DROP TABLE bulky" );

	}

}