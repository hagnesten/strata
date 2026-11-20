#include "test/engine/checks.hh"

namespace test {

	static void folded ( engine::database& db ) {

		rows ( db, "SELECT count ( * ), count ( a ), sum ( a ), min ( a ), max ( a ) FROM fold_rows", "500|471|5132|0|22" );
		rows ( db, "SELECT min ( b ), max ( b ), count ( DISTINCT b ), count ( DISTINCT a ) FROM fold_rows", "w0|w9|31|23" );
		rows ( db, "SELECT sum ( c ), total ( c ), round ( avg ( a ), 6 ) FROM fold_rows WHERE d = 3", "4527.0|4527.0|10.940299" );
		rows ( db, "SELECT count ( * ), sum ( a * d ) FROM fold_rows WHERE d > 2 AND a + d > 10", "191|12182" );
		rows ( db, "SELECT count ( * ), sum ( a ) FROM fold_rows WHERE id BETWEEN 100 AND 200", "101|1058" );
		rows ( db, "SELECT count ( * ), sum ( a ) FROM fold_rows WHERE b LIKE 'w1%'", "177|1919" );
		rows ( db, "SELECT count ( * ), max ( a ) FROM fold_empty", "0|NULL" );
		rows ( db, "SELECT sum ( a ) FROM fold_empty", "NULL" );
		rows ( db, "SELECT count ( * ) FROM fold_rows HAVING count ( * ) > 1000", "" );
		rows ( db, "SELECT count ( * ) + 1, max ( a ) * 2 FROM fold_rows", "501|44" );
		rows ( db, "SELECT group_concat ( b, ',' ) FROM fold_rows WHERE id < 6", "w1,w2,w3,w4,w5" );
		rows ( db, "SELECT max ( a ), b FROM fold_rows", "22|w22" );
		rows ( db, "SELECT count ( * ), min ( c ) FROM fold_rows WHERE a IS NULL", "29|4.25" );

	}

	void fold_tests ( engine::database& db ) {

		section ( "folded aggregates" );
		run ( db, "CREATE TABLE fold_rows ( id INTEGER PRIMARY KEY, a INTEGER, b TEXT, c REAL, d INTEGER )" );
		run ( db, "CREATE TABLE fold_empty ( id INTEGER PRIMARY KEY, a INTEGER )" );
		run ( db, "WITH RECURSIVE n ( i ) AS ( SELECT 1 UNION ALL SELECT i + 1 FROM n WHERE i < 500 ) INSERT INTO fold_rows SELECT i, CASE WHEN i % 17 = 0 THEN NULL ELSE i % 23 END, 'w' || ( i % 31 ), i * 0.25, i % 7 FROM n" );
		folded ( db );
		run ( db, "DROP TABLE fold_empty" );
		run ( db, "DROP TABLE fold_rows" );

	}

}
