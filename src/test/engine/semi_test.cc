#include "test/engine/checks.hh"

namespace test {

	static void shapes ( engine::database& db ) {

		rows ( db, "SELECT count ( * ) FROM semi_outer o WHERE EXISTS ( SELECT 1 FROM semi_inner i WHERE i.ref = o.id AND i.qty > 3 )", "70" );
		rows ( db, "SELECT count ( * ) FROM semi_outer o WHERE NOT EXISTS ( SELECT 1 FROM semi_inner i WHERE i.ref = o.id AND i.qty > 3 )", "130" );
		rows ( db, "SELECT count ( * ) FROM semi_outer o WHERE EXISTS ( SELECT 1 FROM semi_inner i WHERE i.price = o.weight )", "160" );
		rows ( db, "SELECT count ( * ) FROM semi_outer o WHERE EXISTS ( SELECT 1 FROM semi_inner i WHERE i.label = o.name )", "65" );
		rows ( db, "SELECT count ( * ) FROM semi_outer o WHERE EXISTS ( SELECT 1 FROM semi_inner i WHERE i.ref = o.id AND i.qty = o.band )", "76" );
		same_rows ( db, "SELECT id FROM semi_outer o WHERE EXISTS ( SELECT 1 FROM semi_inner i WHERE i.ref = o.id AND i.qty > 3 ) ORDER BY id", "SELECT id FROM semi_outer o WHERE EXISTS ( SELECT 1 FROM semi_inner i WHERE i.ref = o.id AND i.qty > 3 LIMIT 1000 ) ORDER BY id", "a decorrelated EXISTS returns the same rows as a row-by-row one" );
		same_rows ( db, "SELECT id FROM semi_outer o WHERE NOT EXISTS ( SELECT 1 FROM semi_inner i WHERE o.id = i.ref ) ORDER BY id", "SELECT id FROM semi_outer o WHERE NOT EXISTS ( SELECT 1 FROM semi_inner i WHERE o.id = i.ref LIMIT 1000 ) ORDER BY id", "NOT EXISTS with the outer column on the left matches" );
		same_rows ( db, "SELECT id FROM semi_outer o WHERE EXISTS ( SELECT 1 FROM semi_inner i WHERE i.label = o.name ) ORDER BY id", "SELECT id FROM semi_outer o WHERE EXISTS ( SELECT 1 FROM semi_inner i WHERE i.label = o.name LIMIT 1000 ) ORDER BY id", "text keys match" );
		same_rows ( db, "SELECT id FROM semi_outer o WHERE EXISTS ( SELECT 1 FROM semi_inner i WHERE i.ref = o.id AND i.qty = o.band ) ORDER BY id", "SELECT id FROM semi_outer o WHERE EXISTS ( SELECT 1 FROM semi_inner i WHERE i.ref = o.id AND i.qty = o.band LIMIT 1000 ) ORDER BY id", "two key columns match" );
		same_rows ( db, "SELECT id FROM semi_outer o WHERE EXISTS ( SELECT 1 FROM semi_inner i WHERE i.price = o.weight ) ORDER BY id", "SELECT id FROM semi_outer o WHERE EXISTS ( SELECT 1 FROM semi_inner i WHERE i.price = o.weight LIMIT 1000 ) ORDER BY id", "whole reals and integers match" );
		same_rows ( db, "SELECT id FROM semi_outer a WHERE EXISTS ( SELECT 1 FROM semi_outer b WHERE b.band = a.id AND b.id > 150 ) ORDER BY id", "SELECT id FROM semi_outer a WHERE EXISTS ( SELECT 1 FROM semi_outer b WHERE b.band = a.id AND b.id > 150 LIMIT 1000 ) ORDER BY id", "a table can test itself" );
		same_rows ( db, "SELECT id, EXISTS ( SELECT 1 FROM semi_inner i WHERE i.ref = o.id ) FROM semi_outer o ORDER BY id", "SELECT id, EXISTS ( SELECT 1 FROM semi_inner i WHERE i.ref = o.id LIMIT 1000 ) FROM semi_outer o ORDER BY id", "EXISTS works as a result column" );
		rows ( db, "SELECT count ( * ) FROM semi_outer o WHERE EXISTS ( SELECT 1 FROM semi_inner i WHERE i.ref = o.missing )", "0" );
		rows ( db, "SELECT count ( * ) FROM semi_outer o WHERE NOT EXISTS ( SELECT 1 FROM semi_inner i WHERE i.ref = o.missing )", "200" );
		rows ( db, "SELECT count ( * ) FROM semi_outer o WHERE EXISTS ( SELECT 1 FROM semi_inner i WHERE i.ref = o.id AND i.qty > 100 )", "0" );

	}

	void semi_tests ( engine::database& db ) {

		section ( "decorrelated EXISTS" );
		run ( db, "CREATE TABLE semi_outer ( id INTEGER PRIMARY KEY, name TEXT, band INTEGER, weight INTEGER, missing INTEGER )" );
		run ( db, "CREATE TABLE semi_inner ( id INTEGER PRIMARY KEY, ref INTEGER, qty INTEGER, label TEXT, price REAL )" );
		run ( db, "WITH RECURSIVE n ( i ) AS ( SELECT 1 UNION ALL SELECT i + 1 FROM n WHERE i < 200 ) INSERT INTO semi_outer SELECT i, 'n' || ( i % 37 ), i % 6, i % 50, NULL FROM n" );
		run ( db, "WITH RECURSIVE n ( i ) AS ( SELECT 1 UNION ALL SELECT i + 1 FROM n WHERE i < 300 ) INSERT INTO semi_inner SELECT i, CASE WHEN i % 10 = 0 THEN NULL ELSE ( i * 7 ) % 250 END, i % 6, 'n' || ( i % 11 ), ( i % 40 ) * 1.0 + CASE WHEN i % 3 = 0 THEN 0.5 ELSE 0 END FROM n" );
		run ( db, "CREATE INDEX semi_inner_ref ON semi_inner ( ref )" );
		shapes ( db );
		run ( db, "DROP TABLE semi_inner" );
		run ( db, "DROP TABLE semi_outer" );

	}

}
