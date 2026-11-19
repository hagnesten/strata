#include "test/engine/checks.hh"
#include "engine/check/check.hh"

namespace test {

	static bool report_says ( const engine::check_report& report, const char* part ) {

		for ( uint32_t i = 0; i < report.problem_count; i++ )
			if ( contains ( report.problems [ i ], part ) )
				return true;

		return false;

	}

	void check_tests ( engine::database& db ) {

		engine::check_report* report = base::make < engine::check_report > ( 1 );

		if ( !expect ( report != nullptr, "the check report has memory" ) )
			return;

		bool clean = engine::check_database ( db, *report );
		base::text line;

		line.add ( "the whole test database checks clean" );

		if ( !clean && report->problem_count )
			line.add ( ": " ).add ( report->problems [ 0 ] );

		expect ( clean && report->done && report->pages == db.pages.header.page_count, line.value ( ) );

		run ( db, "CREATE TABLE damaged ( a INT, b TEXT )" );
		run ( db, "CREATE INDEX damaged_a ON damaged ( a )" );
		run ( db, "WITH RECURSIVE n ( i ) AS ( SELECT 1 UNION ALL SELECT i + 1 FROM n WHERE i < 300 ) INSERT INTO damaged SELECT i, 'row ' || i FROM n" );

		engine::index_info* index = engine::find_index ( *db.schema, "damaged_a" );
		storage::btree tree = { &db.pages, index ? index->root : 0, true };
		storage::cursor at;
		base::text entry;
		bool found = false;

		if ( index && storage::cursor_first ( at, tree ) && storage::cursor_payload ( at, entry ) && storage::index_delete ( tree, reinterpret_cast < const uint8_t* > ( entry.value ( ) ), entry.size, found ) && found && storage::commit_pager ( db.pages ) ) {

			engine::check_database ( db, *report );
			expect ( report_says ( *report, "damaged_a has 299 entries but damaged has 300 rows" ), "the check notices an index that lost an entry" );

		} else
			expect ( false, "an index entry could be removed behind the engine's back" );

		run ( db, "DROP TABLE damaged" );
		expect ( engine::check_database ( db, *report ), "the database checks clean again after dropping the damage" );
		engine::forget_report ( *report );
		report->memory.~arena ( );
		base::release ( report );

	}

}