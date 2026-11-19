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

}