#include "test/engine/checks.hh"

namespace test {

	static void top_rows ( engine::database& db ) {

		rows ( db, "SELECT id FROM limit_numbers ORDER BY v DESC, id LIMIT 5", "6\n13\n20\n27\n34" );
		rows ( db, "SELECT id FROM limit_numbers ORDER BY v DESC LIMIT 3", "6\n13\n20" );
		rows ( db, "SELECT id FROM limit_numbers ORDER BY v LIMIT 3 OFFSET 2", "21\n28\n35" );
		rows ( db, "SELECT id FROM limit_numbers ORDER BY v DESC, id DESC LIMIT 2", "195\n188" );
		rows ( db, "SELECT id FROM limit_numbers ORDER BY v LIMIT 0", "" );
		rows ( db, "SELECT DISTINCT v FROM limit_numbers ORDER BY v DESC LIMIT 2", "6\n5" );
		rows ( db, "SELECT count ( * ) FROM ( SELECT id FROM limit_numbers ORDER BY w LIMIT 1000 ) AS t", "200" );
		rows ( db, "SELECT w FROM limit_numbers ORDER BY w DESC LIMIT 1", "w99" );
		rows ( db, "SELECT id FROM limit_numbers ORDER BY v, id DESC LIMIT 1 OFFSET 28", "197" );
		rows ( db, "SELECT id FROM limit_numbers WHERE v = 3 ORDER BY id DESC LIMIT 2", "199\n192" );

		engine::database& same = db;
		result_capture whole;
		result_capture bounded;

		query ( same, "SELECT id, v FROM limit_numbers ORDER BY v, w", whole );
		query ( same, "SELECT id, v FROM limit_numbers ORDER BY v, w LIMIT 37", bounded );

		bool prefix = bounded.out.size <= whole.out.size && base::same_bytes ( bounded.out.value ( ), whole.out.value ( ), bounded.out.size );

		expect ( prefix && bounded.rows == 37, "a bounded sort returns the first rows of the full sort" );

	}

	static void subquery_lists ( engine::database& db ) {

		rows ( db, "SELECT count ( * ) FROM limit_numbers WHERE id IN ( SELECT v * 10 FROM limit_numbers )", "6" );
		plan_has ( db, "EXPLAIN QUERY PLAN SELECT count ( * ) FROM limit_numbers WHERE id IN ( SELECT v * 10 FROM limit_numbers )", "rowid IN subquery" );
		rows ( db, "SELECT id FROM limit_numbers WHERE id IN ( SELECT '5' UNION SELECT 7.0 UNION SELECT 9.5 ) ORDER BY id", "5\n7" );
		rows ( db, "SELECT count ( * ) FROM limit_numbers WHERE id IN ( SELECT id FROM limit_numbers WHERE v = 2 ) AND v = 2", "29" );
		rows ( db, "SELECT count ( * ) FROM limit_numbers WHERE id IN ( SELECT id FROM limit_numbers WHERE v = 2 ) AND v = 3", "0" );
		rows ( db, "SELECT count ( * ) FROM limit_numbers WHERE id NOT IN ( SELECT id FROM limit_numbers WHERE v = 2 )", "171" );
		rows ( db, "SELECT count ( * ) FROM limit_numbers WHERE id IN ( SELECT NULL )", "0" );
		rows ( db, "SELECT count ( * ) FROM limit_numbers WHERE id IN ( SELECT id FROM limit_numbers WHERE id > 500 )", "0" );
		rows ( db, "SELECT id FROM limit_numbers WHERE id IN ( SELECT 3 UNION ALL SELECT 3 UNION ALL SELECT 1 )", "1\n3" );
		rows ( db, "SELECT id FROM limit_numbers WHERE v IN ( SELECT '5' ) AND id < 20", "5\n12\n19" );
		rows ( db, "SELECT 5 IN ( SELECT '5' ), '5' IN ( SELECT 5 )", "0|0" );
		rows ( db, "SELECT count ( * ) FROM limit_numbers WHERE w IN ( SELECT 'w' || id FROM limit_numbers WHERE v = 1 )", "29" );

	}

	void limit_tests ( engine::database& db ) {

		section ( "bounded sorts and subquery lists" );
		run ( db, "CREATE TABLE limit_numbers ( id INTEGER PRIMARY KEY, v INTEGER, w TEXT )" );
		run ( db, "WITH RECURSIVE n ( i ) AS ( SELECT 1 UNION ALL SELECT i + 1 FROM n WHERE i < 200 ) INSERT INTO limit_numbers SELECT i, i % 7, 'w' || i FROM n" );
		top_rows ( db );
		subquery_lists ( db );
		run ( db, "DROP TABLE limit_numbers" );

	}

}
