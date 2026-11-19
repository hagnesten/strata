#include "test/engine/checks.hh"

#define JOIN_ROWS 14
#define JOIN_ROUNDS 160

namespace test {

	static void join_value ( base::text& sql, test_random& random ) {

		if ( random.below ( 6 ) == 0 )
			sql.add ( "NULL" );
		else
			sql.number ( random.below ( 6 ) );

	}

	static void join_condition ( base::text& sql, test_random& random ) {

		static const char* const shapes [ 6 ] = { "a.k = b.k", "a.k = b.k AND b.w > 2", "a.k < b.k", "a.k = b.k OR a.v = b.w", "b.k = a.v", "a.k + 1 = b.k AND a.v IS NOT NULL" };

		sql.add ( shapes [ random.below ( 6 ) ] );

	}

	static void join_filter ( base::text& sql, test_random& random ) {

		static const char* const shapes [ 6 ] = { "1", "a.v IS NULL", "b.w > 1", "a.k IS NOT b.k", "coalesce ( a.v, 9 ) <> 3", "b.k IS NULL OR a.k IS NULL" };

		sql.add ( shapes [ random.below ( 6 ) ] );

	}

	static bool same_answer ( engine::database& db, const char* first, const char* second, uint32_t& mismatches ) {

		result_capture one;
		result_capture two;

		if ( !query ( db, first, one ) || !query ( db, second, two ) ) {

			base::text line;

			line.add ( first ).add ( "\n          error: " ).add ( db.problem.value ( ) );
			expect ( false, line.value ( ) );

			return false;

		}

		if ( base::same ( one.out.value ( ), two.out.value ( ) ) )
			return true;

		if ( mismatches++ < 3 ) {

			base::text line;

			line.add ( first ).add ( "\n          got:    " ).add ( one.out.value ( ) ).add ( "\n          wanted: " ).add ( two.out.value ( ) ).add ( "\n          from:   " ).add ( second );
			expect ( false, line.value ( ) );

		}

		return true;

	}

	void join_fuzz ( engine::database& db ) {

		test_random random = { 0x3C6EF372FE94F82Bull };
		base::text sql;
		uint32_t mismatches = 0;
		uint32_t checked = 0;
		bool fine = true;

		run ( db, "CREATE TABLE ja ( k INT, v INT )" );
		run ( db, "CREATE TABLE jb ( k INT, w INT )" );
		run ( db, "CREATE INDEX jb_k ON jb ( k )" );

		for ( uint32_t round = 0; round < JOIN_ROUNDS && fine; round++ ) {

			if ( round % 20 == 0 ) {

				run ( db, "DELETE FROM ja; DELETE FROM jb" );

				for ( uint32_t r = 0; r < JOIN_ROWS; r++ ) {

					sql.clear ( );
					sql.add ( "INSERT INTO ja VALUES ( " );
					join_value ( sql, random );
					sql.add ( ", " );
					join_value ( sql, random );
					sql.add ( " ); INSERT INTO jb VALUES ( " );
					join_value ( sql, random );
					sql.add ( ", " );
					join_value ( sql, random );
					sql.add ( " )" );
					fine = fine && engine::execute ( db, sql.value ( ), nullptr, 0, nullptr );

				}

			}

			base::text on;
			base::text where;
			base::text right;
			base::text swapped;
			base::text full;
			base::text halves;

			join_condition ( on, random );
			join_filter ( where, random );

			right.add ( "SELECT a.rowid, b.rowid FROM ja a RIGHT JOIN jb b ON " ).add ( on.value ( ) ).add ( " WHERE " ).add ( where.value ( ) ).add ( " ORDER BY 1, 2" );
			swapped.add ( "SELECT a.rowid, b.rowid FROM jb b LEFT JOIN ja a ON " ).add ( on.value ( ) ).add ( " WHERE " ).add ( where.value ( ) ).add ( " ORDER BY 1, 2" );
			full.add ( "SELECT a.rowid, b.rowid FROM ja a FULL JOIN jb b ON " ).add ( on.value ( ) ).add ( " WHERE " ).add ( where.value ( ) ).add ( " ORDER BY 1, 2" );
			halves.add ( "SELECT a.rowid, b.rowid FROM ja a LEFT JOIN jb b ON " ).add ( on.value ( ) ).add ( " WHERE " ).add ( where.value ( ) );
			halves.add ( " UNION ALL SELECT a.rowid, b.rowid FROM jb b LEFT JOIN ja a ON " ).add ( on.value ( ) ).add ( " WHERE a.rowid IS NULL AND ( " ).add ( where.value ( ) ).add ( " ) ORDER BY 1, 2" );

			fine = same_answer ( db, right.value ( ), swapped.value ( ), mismatches ) && same_answer ( db, full.value ( ), halves.value ( ), mismatches );
			checked += 2;

		}

		base::text summary;

		summary.number ( checked ).add ( " random outer joins agree with their LEFT JOIN forms" );
		expect ( fine && !mismatches && checked == JOIN_ROUNDS * 2, summary.value ( ) );

	}

	void join_order_tests ( engine::database& db ) {

		run ( db, "CREATE TABLE big_side ( id INTEGER PRIMARY KEY, k INT, v TEXT )" );
		run ( db, "WITH RECURSIVE n ( i ) AS ( SELECT 1 UNION ALL SELECT i + 1 FROM n WHERE i < 2000 ) INSERT INTO big_side SELECT i, i % 100, 'v' || i FROM n" );
		run ( db, "CREATE TABLE small_side ( k INT, label TEXT )" );
		run ( db, "INSERT INTO small_side VALUES ( 5, 'five' ), ( 10, 'ten' ), ( 15, 'fifteen' )" );
		plan_has ( db, "EXPLAIN QUERY PLAN SELECT b.v FROM big_side b, small_side s WHERE b.id = s.k", "1|0|SCAN small_side AS s\n2|0|SEARCH big_side AS b USING ROWID (rowid=?)" );
		plan_has ( db, "EXPLAIN QUERY PLAN SELECT b.v FROM big_side b JOIN small_side s ON s.k = b.id", "1|0|SCAN small_side AS s\n2|0|SEARCH big_side AS b USING ROWID (rowid=?)" );
		plan_has ( db, "EXPLAIN QUERY PLAN SELECT b.v FROM big_side b CROSS JOIN small_side s WHERE b.id = s.k", "1|0|SCAN big_side AS b\n2|0|SCAN small_side AS s" );
		plan_has ( db, "EXPLAIN QUERY PLAN SELECT b.v FROM big_side b LEFT JOIN small_side s ON b.id = s.k", "1|0|SCAN big_side AS b" );
		rows ( db, "SELECT s.label, b.v FROM big_side b, small_side s WHERE b.id = s.k ORDER BY b.id", "five|v5\nten|v10\nfifteen|v15" );
		rows ( db, "SELECT * FROM big_side b, small_side s WHERE b.id = s.k AND s.k = 10", "10|10|v10|10|ten" );
		fails ( db, "SELECT 1 FROM big_side b JOIN small_side s ON s.k = c.k JOIN small_side c", "only use tables joined before it" );
		run ( db, "CREATE INDEX big_k ON big_side ( k )" );
		run ( db, "ANALYZE" );
		rows ( db, "SELECT idx, stat FROM strata_stat WHERE tbl = 'big_side' ORDER BY idx", "NULL|2000\nbig_k|2000 20" );
		rows ( db, "SELECT stat FROM strata_stat WHERE tbl = 'small_side'", "3" );
		plan_has ( db, "EXPLAIN QUERY PLAN SELECT count ( * ) FROM big_side b, small_side s WHERE b.k = s.k", "1|0|SCAN small_side AS s\n2|0|SEARCH big_side AS b USING COVERING INDEX big_k (k=?)" );
		rows ( db, "SELECT count ( * ) FROM big_side b, small_side s WHERE b.k = s.k", "60" );
		run ( db, "ANALYZE" );
		rows ( db, "SELECT count ( * ) FROM strata_stat WHERE tbl IN ( 'big_side', 'small_side' )", "3" );
		run ( db, "DROP TABLE small_side" );
		run ( db, "DROP TABLE big_side" );

	}

	void join_order_fuzz ( engine::database& db ) {

		static const char* const shapes [ 8 ] = { "a.k = b.k", "c.k = a.v", "c.rowid = b.w", "b.k = 3", "c.w > 2", "a.v = c.w AND b.w = c.k", "c.k = a.k + 1", "a.rowid = c.w" };
		test_random random = { 0x510E527FADE682D1ull };
		base::text sql;
		uint32_t mismatches = 0;
		bool fine = true;

		run ( db, "CREATE TABLE jc ( k INT, w INT )" );
		run ( db, "CREATE INDEX jc_k ON jc ( k )" );

		for ( uint32_t r = 0; r < 300 && fine; r++ ) {

			sql.clear ( );
			sql.add ( "INSERT INTO jc VALUES ( " ).number ( random.below ( 8 ) ).add ( ", " ).number ( random.below ( 20 ) ).add ( " )" );
			fine = engine::execute ( db, sql.value ( ), nullptr, 0, nullptr );

		}

		for ( uint32_t round = 0; round < 120 && fine; round++ ) {

			base::text where;
			uint32_t parts = 1 + random.below ( 3 );

			for ( uint32_t p = 0; p < parts; p++ ) {

				if ( p )
					where.add ( " AND " );

				where.add ( shapes [ random.below ( 8 ) ] );

			}

			base::text free;
			base::text fixed;

			free.add ( "SELECT a.rowid, b.rowid, c.rowid FROM jc c, ja a, jb b WHERE " ).add ( where.value ( ) ).add ( " ORDER BY 1, 2, 3" );
			fixed.add ( "SELECT a.rowid, b.rowid, c.rowid FROM jc c CROSS JOIN ja a CROSS JOIN jb b WHERE " ).add ( where.value ( ) ).add ( " ORDER BY 1, 2, 3" );
			fine = same_answer ( db, free.value ( ), fixed.value ( ), mismatches );

		}

		expect ( fine && !mismatches, "reordered inner joins agree with the written order" );
		run ( db, "DROP TABLE jc" );

	}

}