#include "test/engine/checks.hh"

namespace test {

	static void bounded ( engine::database& db ) {

		plan_has ( db, "EXPLAIN QUERY PLAN SELECT id FROM range_rows WHERE a BETWEEN 3 AND 7", "range_a" );
		same_rows ( db, "SELECT id FROM range_rows WHERE a BETWEEN 3 AND 7 ORDER BY id", "SELECT id FROM range_rows WHERE +a BETWEEN 3 AND 7 ORDER BY id", "an index BETWEEN matches a scan" );
		same_rows ( db, "SELECT id FROM range_rows WHERE a > 3 AND a <= 7 ORDER BY id", "SELECT id FROM range_rows WHERE +a > 3 AND +a <= 7 ORDER BY id", "an index range with two bounds matches a scan" );
		same_rows ( db, "SELECT id FROM range_rows WHERE a < 4 ORDER BY id", "SELECT id FROM range_rows WHERE +a < 4 ORDER BY id", "an upper bound alone leaves NULL rows out" );
		same_rows ( db, "SELECT id FROM range_rows WHERE a > 8 ORDER BY id", "SELECT id FROM range_rows WHERE +a > 8 ORDER BY id", "a lower bound alone keeps text values above numbers" );
		same_rows ( db, "SELECT id FROM range_rows WHERE a = 5 ORDER BY id", "SELECT id FROM range_rows WHERE +a = 5 ORDER BY id", "an index equality matches a scan" );
		same_rows ( db, "SELECT id FROM range_rows WHERE a = '5' ORDER BY id", "SELECT id FROM range_rows WHERE +a = 5 ORDER BY id", "text compared to a numeric column turns numeric" );
		same_rows ( db, "SELECT id FROM range_rows WHERE a BETWEEN 2.5 AND 4.5 ORDER BY id", "SELECT id FROM range_rows WHERE +a BETWEEN 2.5 AND 4.5 ORDER BY id", "real bounds on whole numbers match" );
		same_rows ( db, "SELECT id FROM range_rows WHERE g = 2 AND a BETWEEN 1 AND 6 ORDER BY id", "SELECT id FROM range_rows WHERE +g = 2 AND +a BETWEEN 1 AND 6 ORDER BY id", "a range after an equality prefix matches" );
		same_rows ( db, "SELECT id FROM range_rows WHERE g = 2 AND a < 3 ORDER BY id", "SELECT id FROM range_rows WHERE +g = 2 AND +a < 3 ORDER BY id", "an upper bound after an equality prefix leaves NULL rows out" );
		same_rows ( db, "SELECT id FROM range_rows WHERE w >= 'm' AND w < 'p' ORDER BY id", "SELECT id FROM range_rows WHERE +w >= 'm' AND +w < 'p' ORDER BY id", "a text range matches a scan" );
		same_rows ( db, "SELECT count ( * ), sum ( a ) FROM range_rows WHERE a BETWEEN 0 AND 100 AND id % 3 = 0", "SELECT count ( * ), sum ( a ) FROM range_rows WHERE +a BETWEEN 0 AND 100 AND id % 3 = 0", "other filters still apply beside an index range" );
		rows ( db, "SELECT count ( * ) FROM range_rows WHERE a < 4", "132" );
		rows ( db, "SELECT count ( * ) FROM range_rows WHERE a > 8", "77" );

	}

	void range_tests ( engine::database& db ) {

		section ( "index ranges" );
		run ( db, "CREATE TABLE range_rows ( id INTEGER PRIMARY KEY, a INTEGER, g INTEGER, w TEXT )" );
		run ( db, "WITH RECURSIVE n ( i ) AS ( SELECT 1 UNION ALL SELECT i + 1 FROM n WHERE i < 400 ) INSERT INTO range_rows SELECT i, CASE WHEN i % 13 = 0 THEN NULL WHEN i % 29 = 0 THEN 'text' || i ELSE i % 11 END, i % 4, substr ( 'abcdefghijklmnopqrstuvwxyz', 1 + i % 26, 1 ) || i FROM n" );
		run ( db, "CREATE INDEX range_a ON range_rows ( a )" );
		run ( db, "CREATE INDEX range_ga ON range_rows ( g, a )" );
		run ( db, "CREATE INDEX range_w ON range_rows ( w )" );
		bounded ( db );
		run ( db, "DROP TABLE range_rows" );

	}

}
