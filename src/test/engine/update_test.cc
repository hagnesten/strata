#include "test/engine/checks.hh"
#include "engine/check/check.hh"

namespace test {

	static void growing_rows ( engine::database& db ) {

		run ( db, "UPDATE pass_rows SET w = w || '0123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789' WHERE id % 2 = 0" );
		rows ( db, "SELECT count ( * ) FROM pass_rows", "3000" );
		rows ( db, "SELECT count ( * ) FROM pass_rows WHERE length ( w ) > 100", "1500" );
		rows ( db, "SELECT sum ( v ) FROM pass_rows", "6000" );
		run ( db, "UPDATE pass_rows SET v = v + 10, w = w || 'abcdefghijabcdefghijabcdefghijabcdefghijabcdefghijabcdefghijabcdefghijabcdefghijabcdefghijabcdefghij' WHERE id > 1000" );
		rows ( db, "SELECT sum ( v ) FROM pass_rows", "26000" );
		rows ( db, "SELECT count ( * ) FROM pass_rows WHERE v >= 10", "2000" );
		rows ( db, "SELECT count ( * ) FROM pass_rows WHERE v = 12", "400" );
		rows ( db, "SELECT count ( * ) FROM pass_rows WHERE v + 0 = 12", "400" );

	}

	static void every_row_once ( engine::database& db ) {

		run ( db, "UPDATE pass_rows SET v = v + 1" );
		rows ( db, "SELECT sum ( v ) FROM pass_rows", "29000" );
		run ( db, "UPDATE pass_rows SET v = v - 1 WHERE id BETWEEN 100 AND 199" );
		rows ( db, "SELECT sum ( v ) FROM pass_rows", "28900" );
		run ( db, "UPDATE pass_rows SET w = 'one' WHERE id = 7" );
		rows ( db, "SELECT w FROM pass_rows WHERE id = 7", "one" );
		run ( db, "UPDATE pass_rows SET w = 'none' WHERE id = 99999" );
		rows ( db, "SELECT count ( * ) FROM pass_rows WHERE w = 'none'", "0" );

	}

	static void rolled_back ( engine::database& db ) {

		run ( db, "BEGIN" );
		run ( db, "UPDATE pass_rows SET w = w || w || w, v = v * 2" );
		rows ( db, "SELECT sum ( v ) FROM pass_rows", "57800" );
		run ( db, "ROLLBACK" );
		rows ( db, "SELECT sum ( v ) FROM pass_rows", "28900" );
		rows ( db, "SELECT count ( * ) FROM pass_rows WHERE length ( w ) > 400", "0" );
		fails ( db, "UPDATE pass_rows SET v = NULL WHERE id > 2990", "NOT NULL" );
		rows ( db, "SELECT count ( * ) FROM pass_rows WHERE v IS NULL", "0" );

	}

	void update_tests ( engine::database& db ) {

		section ( "one-pass updates" );
		run ( db, "CREATE TABLE pass_rows ( id INTEGER PRIMARY KEY, v INTEGER NOT NULL, w TEXT )" );
		run ( db, "WITH RECURSIVE n ( i ) AS ( SELECT 1 UNION ALL SELECT i + 1 FROM n WHERE i < 3000 ) INSERT INTO pass_rows SELECT i, i % 5, 'x' || i FROM n" );
		run ( db, "CREATE INDEX pass_v ON pass_rows ( v )" );
		growing_rows ( db );
		every_row_once ( db );
		rolled_back ( db );

		engine::check_report* report = base::make < engine::check_report > ( 1 );

		if ( expect ( report != nullptr, "the update check has memory" ) )
			expect ( engine::check_database ( db, *report ), "the tables and indexes check clean after one-pass updates" );

		base::release ( report );
		run ( db, "DROP TABLE pass_rows" );

	}

}
