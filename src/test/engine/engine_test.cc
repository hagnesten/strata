#include "test/engine/checks.hh"

namespace test {

	static void value_tests ( engine::database& db ) {

		rows ( db, "SELECT 1 + 2, 7 / 2, 7.0 / 2, 1 / 0, 5 % 3, -5 % 3", "3|3|3.5|NULL|2|-2" );
		rows ( db, "SELECT 'a' || 1 || 2.5, NULL || 'x', 2 * 3 + 4, 2 * ( 3 + 4 )", "a12.5|NULL|10|14" );
		rows ( db, "SELECT 10 > 9, '10' > 9, 'abc' < 'abd', NULL = NULL, NULL IS NULL, 1 IS 1, 1 IS NOT NULL", "1|1|1|NULL|1|1|1" );
		rows ( db, "SELECT typeof ( 1 ), typeof ( 1.5 ), typeof ( 'x' ), typeof ( NULL ), typeof ( x'00' )", "integer|real|text|null|blob" );
		rows ( db, "SELECT CAST ( '12abc' AS INTEGER ), CAST ( 3.9 AS INTEGER ), CAST ( 5 AS TEXT ) || 'x', CAST ( '1e3' AS REAL ), CAST ( '7' AS NUMERIC ) + 1", "12|3|5x|1000.0|8" );
		rows ( db, "SELECT 9223372036854775807 + 1, -9223372036854775807 - 1, 9223372036854775807 * 2", "9.22337203685478e+18|-9223372036854775808|1.84467440737096e+19" );
		rows ( db, "SELECT 0.1 + 0.2, 1e100, 1.5e-7, 100.0, -0.5, 123456.789", "0.3|1.0e+100|1.5e-07|100.0|-0.5|123456.789" );
		rows ( db, "SELECT 1 AND NULL, 0 AND NULL, 1 OR NULL, 0 OR NULL, NOT NULL, NOT 0", "NULL|0|1|NULL|NULL|1" );
		rows ( db, "SELECT 6 & 3, 6 | 3, ~5, 1 << 4, 256 >> 4, -16 >> 2", "2|7|-6|16|16|-4" );
		rows ( db, "SELECT round ( 2.5 ), round ( -2.5 ), round ( 3.14159, 2 ), abs ( -7 ), abs ( -7.5 ), sign ( -3 ), sign ( 'x' )", "3.0|-3.0|3.14|7|7.5|-1|NULL" );
		rows ( db, "SELECT length ( 'h\xC3\xA9llo' ), upper ( 'abc' ), lower ( 'ABC' ), substr ( 'hello', 2, 3 ), substr ( 'hello', -3 ), substr ( 'hello', 0, 2 )", "5|ABC|abc|ell|llo|h" );
		rows ( db, "SELECT trim ( '  x  ' ), ltrim ( 'xxy', 'x' ), rtrim ( 'yxx', 'x' ), replace ( 'aaa', 'a', 'bb' ), instr ( 'hello', 'll' ), instr ( 'hello', 'z' )", "x|y|y|bbbbbb|3|0" );
		rows ( db, "SELECT coalesce ( NULL, NULL, 3 ), ifnull ( NULL, 'd' ), nullif ( 1, 1 ), nullif ( 1, 2 ), iif ( 1 > 2, 'a', 'b' )", "3|d|NULL|1|b" );
		rows ( db, "SELECT hex ( 'AB' ), quote ( 'it''s' ), quote ( NULL ), quote ( x'0aff' ), char ( 72, 105 ), unicode ( 'A' )", "4142|'it''s'|NULL|X'0AFF'|Hi|65" );
		rows ( db, "SELECT printf ( '%5.2f|%-4d|%04d|%s|%x|%%', 3.14159, 7, 42, 'x', 255 )", " 3.14|7   |0042|x|ff|%" );
		rows ( db, "SELECT max ( 1, 5, 3 ), min ( 'b', 'a' ), max ( 1, NULL ), concat ( 'a', NULL, 'b' ), concat_ws ( '-', 'a', NULL, 'b' )", "5|a|NULL|ab|a-b" );
		rows ( db, "SELECT 'Hello' LIKE 'h%', 'Hello' LIKE 'h_llo', 'Hello' GLOB 'h*', 'Hello' GLOB 'H*', 'a%b' LIKE 'a\\%b' ESCAPE '\\', 'abc' NOT LIKE '%b%'", "1|1|0|1|1|0" );
		rows ( db, "SELECT 5 BETWEEN 1 AND 10, 5 NOT BETWEEN 1 AND 4, 3 IN ( 1, 2, 3 ), 4 IN ( 1, 2, NULL ), NULL IN ( 1 ), 4 NOT IN ( 1, 2 )", "1|1|1|NULL|NULL|1" );
		rows ( db, "SELECT CASE 2 WHEN 1 THEN 'a' WHEN 2 THEN 'b' ELSE 'c' END, CASE WHEN 0 THEN 'x' END, CASE WHEN 1 > 0 THEN 'yes' ELSE 'no' END", "b|NULL|yes" );
		rows ( db, "SELECT zeroblob ( 3 ), length ( randomblob ( 8 ) ), typeof ( random ( ) ), strata_version ( )", "X'000000'|8|integer|" ENGINE_VERSION );
		rows ( db, "SELECT typeof ( -9223372036854775808 ), -9223372036854775808, CAST ( -9223372036854775808.0 AS INTEGER )", "integer|-9223372036854775808|-9223372036854775808" );
		rows ( db, "\xEF\xBB\xBFSELECT 'after a byte order mark'", "after a byte order mark" );
		rows ( db, "SELECT printf ( '%.3c|%5s|%-3s|', 'ab', 'x', 'y' )", "aaa|    x|y  |" );
		rows ( db, "SELECT printf ( '%g|%g|%g|%g|%g|%.3g|%#g', 2.0, 0.0001, 0.00001, 1234567, 100000, 3.14159, 2.0 )", "2|0.0001|1e-05|1.23457e+06|100000|3.14|2.00000" );
		rows ( db, "SELECT printf ( '%e|%.2e|%E|%+.1e|%10.3g|', 12345.678, 0.000123, 0, 5, -0.5 )", "1.234568e+04|1.23e-04|0.000000E+00|+5.0e+00|      -0.5|" );
		rows ( db, "SELECT date ( '2024-03-15' ), datetime ( '2024-03-15 10:20:30', '+1 day', '-2 hours' ), date ( '2024-01-31', '+1 month' ), time ( '2024-03-15T07:08:09Z' )", "2024-03-15|2024-03-16 08:20:30|2024-03-02|07:08:09" );
		rows ( db, "SELECT strftime ( '%Y/%m/%d %H:%M %j %w %u', '2024-02-29 13:05:00' ), julianday ( '2000-01-01 12:00:00' ), unixepoch ( '1970-01-02' )", "2024/02/29 13:05 060 4 4|2451545.0|86400" );
		rows ( db, "SELECT datetime ( 1700000000, 'unixepoch' ), date ( '2024-03-15', 'start of month' ), date ( '2024-03-15', 'start of year' ), date ( '2024-03-15', 'weekday 0' ), date ( '2024-12-31', '+1 day' )", "2023-11-14 22:13:20|2024-03-01|2024-01-01|2024-03-17|2025-01-01" );
		rows ( db, "SELECT typeof ( CURRENT_TIMESTAMP ), length ( CURRENT_DATE ), length ( time ( 'now' ) ), date ( 'not a date' ), date ( '2024-02-30' ) IS NOT NULL, strftime ( '%f', '12:00:01.250' )", "text|10|8|NULL|1|01.250" );
		rows ( db, "SELECT json_extract ( '{\"a\":{\"b\":[1,2,{\"c\":\"x\"}]}}', '$.a.b[2].c' ), '{\"a\":[1,2,3]}' -> '$.a', '{\"a\":[1,2,3]}' ->> '$.a[#-1]', '{\"x\":\"\\u00e9\"}' ->> 'x'", "x|[1,2,3]|3|\xC3\xA9" );
		rows ( db, "SELECT json_array ( 1, 'two', NULL, json ( '{\"k\": true}' ) ), json_object ( 'n', 1, 'list', json_array ( 1, 2 ) ), json ( ' { \"a\" : 1 } ' )", "[1,\"two\",null,{\"k\":true}]|{\"n\":1,\"list\":[1,2]}|{\"a\":1}" );
		rows ( db, "SELECT json_type ( '{\"a\":1.5}', '$.a' ), json_array_length ( '[1,[2,3]]' ), json_valid ( '{\"a\":' ), json_valid ( '[1, 2]' ), json_extract ( '[1,2]', '$[0]', '$[5]' ), json_quote ( 'say \"hi\"' )", "real|2|0|1|[1,null]|\"say \\\"hi\\\"\"" );
		rows ( db, "SELECT json_group_array ( column1 ) FROM ( VALUES ( 1 ), ( 'b' ), ( NULL ) ) AS v", "[1,\"b\",null]" );
		fails ( db, "SELECT json_extract ( '{bad', '$' )", "malformed JSON" );
		rows ( db, "VALUES ( 1, 'a' ), ( 2, 'b' )", "1|a\n2|b" );
		rows ( db, "SELECT * FROM ( VALUES ( 3 ), ( 1 ), ( 2 ) ) AS v ORDER BY column1 DESC", "3\n2\n1" );
		fails ( db, "SELECT nosuch ( 1 )", "no such function: nosuch" );
		fails ( db, "SELECT length ( 1, 2 )", "wrong number of arguments" );
		fails ( db, "SELECT x", "no such column: x" );
		fails ( db, "SELECT sum ( 1 ) WHERE count ( * ) > 1", "cannot be used here" );
		fails ( db, "SELECT 1 FROM nowhere", "no such table: nowhere" );
		fails ( db, "SELECT ( SELECT 1, 2 )", "returns 2 columns" );
		fails ( db, "SELECT 1 UNION SELECT 1, 2", "same number of columns" );
		fails ( db, "SELECT 1 ORDER BY 2", "out of range" );

	}

	static void table_tests ( engine::database& db ) {

		run ( db, "CREATE TABLE people ( id INTEGER PRIMARY KEY, name TEXT NOT NULL, age INT, city TEXT )" );
		run ( db, "INSERT INTO people ( name, age, city ) VALUES ( 'Ada', 36, 'London' ), ( 'Linus', 28, 'Helsinki' ), ( 'Grace', 45, 'New York' ), ( 'Alan', 41, 'London' ), ( 'Barbara', NULL, 'Boston' )" );
		rows ( db, "SELECT last_insert_rowid ( ), changes ( )", "5|5" );
		rows ( db, "SELECT id, name FROM people WHERE age > 30 ORDER BY age DESC", "3|Grace\n4|Alan\n1|Ada" );
		rows ( db, "SELECT count ( * ), sum ( 1 ), total ( NULL ), max ( NULL )", "1|1|0.0|NULL" );
		rows ( db, "SELECT count ( * ), count ( age ), sum ( age ), avg ( age ), min ( age ), max ( age ) FROM people", "5|4|150|37.5|28|45" );
		rows ( db, "SELECT city, count ( * ) FROM people GROUP BY city", "Boston|1\nHelsinki|1\nLondon|2\nNew York|1" );
		rows ( db, "SELECT city, count ( * ) AS n FROM people GROUP BY city HAVING n > 1", "London|2" );
		rows ( db, "SELECT DISTINCT city FROM people ORDER BY 1 LIMIT 2 OFFSET 1", "Helsinki\nLondon" );
		rows ( db, "SELECT name FROM people ORDER BY age LIMIT 2", "Barbara\nLinus" );
		rows ( db, "SELECT name FROM people ORDER BY age DESC, name LIMIT 1", "Grace" );
		rows ( db, "SELECT group_concat ( name, ', ' ) FROM people WHERE city = 'London'", "Ada, Alan" );
		rows ( db, "SELECT name, max ( age ) FROM people", "Grace|45" );
		rows ( db, "SELECT name, min ( age ) FROM people", "Linus|28" );
		rows ( db, "SELECT upper ( name ) AS shout FROM people WHERE id IN ( 1, 3 ) ORDER BY shout", "ADA\nGRACE" );
		rows ( db, "SELECT count ( DISTINCT city ), sum ( DISTINCT age > 30 ) FROM people", "4|1" );
		rows ( db, "SELECT city, group_concat ( name ) FROM people GROUP BY 1 ORDER BY count ( * ) DESC, city LIMIT 2", "London|Ada,Alan\nBoston|Barbara" );
		rows ( db, "SELECT name FROM people WHERE name LIKE 'a%' ORDER BY name", "Ada\nAlan" );
		rows ( db, "SELECT name FROM people WHERE age IS NULL", "Barbara" );
		rows ( db, "SELECT name FROM people WHERE id = '2'", "Linus" );
		rows ( db, "SELECT name FROM people WHERE age = '45'", "Grace" );
		rows ( db, "SELECT rowid, oid, _rowid_ FROM people WHERE id = 4", "4|4|4" );
		rows ( db, "SELECT name FROM people WHERE id > 2 AND id <= 4", "Grace\nAlan" );
		rows ( db, "SELECT name FROM people WHERE id BETWEEN 2.5 AND 4", "Grace\nAlan" );
		rows ( db, "SELECT count ( * ) FROM people WHERE id IN ( 5, 1, 1, 99 )", "2" );
		rows ( db, "SELECT city, avg ( age ) FROM people GROUP BY city HAVING avg ( age ) > 30 ORDER BY 2 DESC", "New York|45.0\nLondon|38.5" );
		rows ( db, "SELECT count ( * ) FROM people WHERE 1 = 0", "0" );
		rows ( db, "SELECT city FROM people WHERE 1 = 0 GROUP BY city", "" );

	}

	static void join_tests ( engine::database& db ) {

		run ( db, "CREATE TABLE pets ( id INTEGER PRIMARY KEY, owner INT REFERENCES people ( id ) ON DELETE CASCADE, kind TEXT )" );
		run ( db, "INSERT INTO pets ( owner, kind ) VALUES ( 1, 'cat' ), ( 1, 'dog' ), ( 3, 'parrot' )" );
		rows ( db, "SELECT p.name, x.kind FROM people p JOIN pets x ON x.owner = p.id ORDER BY x.id", "Ada|cat\nAda|dog\nGrace|parrot" );
		rows ( db, "SELECT p.name, x.kind FROM people AS p, pets AS x WHERE x.owner = p.id AND x.kind <> 'dog' ORDER BY 1", "Ada|cat\nGrace|parrot" );
		rows ( db, "SELECT p.name, count ( x.id ) FROM people p LEFT JOIN pets x ON x.owner = p.id GROUP BY p.id ORDER BY p.id", "Ada|2\nLinus|0\nGrace|1\nAlan|0\nBarbara|0" );
		rows ( db, "SELECT p.name FROM people p LEFT JOIN pets x ON x.owner = p.id WHERE x.id IS NULL ORDER BY p.name", "Alan\nBarbara\nLinus" );
		rows ( db, "SELECT count ( * ) FROM people CROSS JOIN pets", "15" );
		rows ( db, "SELECT name FROM people WHERE id NOT IN ( SELECT owner FROM pets ) ORDER BY id", "Linus\nAlan\nBarbara" );
		rows ( db, "SELECT name FROM people p WHERE EXISTS ( SELECT 1 FROM pets WHERE owner = p.id ) ORDER BY 1", "Ada\nGrace" );
		rows ( db, "SELECT name, ( SELECT count ( * ) FROM pets WHERE owner = people.id ) FROM people ORDER BY id LIMIT 3", "Ada|2\nLinus|0\nGrace|1" );
		rows ( db, "SELECT name FROM people WHERE age > ( SELECT avg ( age ) FROM people ) ORDER BY name", "Alan\nGrace" );
		rows ( db, "SELECT * FROM ( SELECT city, count ( * ) AS n FROM people GROUP BY city ) AS c WHERE n = 1 ORDER BY city", "Boston|1\nHelsinki|1\nNew York|1" );
		rows ( db, "SELECT name FROM people WHERE id IN ( SELECT owner FROM pets WHERE kind = 'parrot' )", "Grace" );
		rows ( db, "SELECT kind, ( SELECT name FROM people WHERE id = pets.owner ) FROM pets ORDER BY kind", "cat|Ada\ndog|Ada\nparrot|Grace" );
		run ( db, "CREATE TABLE left_side ( k INT, x TEXT )" );
		run ( db, "CREATE TABLE right_side ( k INT, y TEXT )" );
		run ( db, "INSERT INTO left_side VALUES ( 1, 'a' ), ( 2, 'b' ), ( 3, 'c' )" );
		run ( db, "INSERT INTO right_side VALUES ( 2, 'B' ), ( 3, 'C' ), ( 3, 'CC' ), ( 4, 'D' )" );
		rows ( db, "SELECT * FROM left_side JOIN right_side USING ( k ) ORDER BY k, y", "2|b|B\n3|c|C\n3|c|CC" );
		rows ( db, "SELECT k, x, y FROM left_side LEFT JOIN right_side USING ( k ) ORDER BY k, y", "1|a|NULL\n2|b|B\n3|c|C\n3|c|CC" );
		rows ( db, "SELECT l.x, r.y FROM left_side l LEFT JOIN right_side r ON r.k = l.k AND r.y <> 'C' ORDER BY l.x", "a|NULL\nb|B\nc|CC" );
		fails ( db, "SELECT k FROM left_side, right_side", "ambiguous" );
		rows ( db, "SELECT l.x, r.y FROM left_side l RIGHT JOIN right_side r ON r.k = l.k ORDER BY r.y", "b|B\nc|C\nc|CC\nNULL|D" );
		rows ( db, "SELECT l.x, r.y FROM left_side l FULL OUTER JOIN right_side r ON r.k = l.k ORDER BY l.x, r.y", "NULL|D\na|NULL\nb|B\nc|C\nc|CC" );
		rows ( db, "SELECT k, x, y FROM left_side RIGHT JOIN right_side USING ( k ) ORDER BY k, y", "2|b|B\n3|c|C\n3|c|CC\n4|NULL|D" );
		rows ( db, "SELECT k, x, y FROM left_side FULL JOIN right_side USING ( k ) ORDER BY k, y", "1|a|NULL\n2|b|B\n3|c|C\n3|c|CC\n4|NULL|D" );
		rows ( db, "SELECT * FROM left_side NATURAL JOIN right_side ORDER BY y", "2|b|B\n3|c|C\n3|c|CC" );
		rows ( db, "SELECT * FROM left_side NATURAL FULL JOIN right_side ORDER BY k, y", "1|a|NULL\n2|b|B\n3|c|C\n3|c|CC\n4|NULL|D" );
		rows ( db, "SELECT l.x, r.y FROM left_side l RIGHT JOIN right_side r ON r.k = l.k WHERE l.x IS NULL", "NULL|D" );
		rows ( db, "SELECT r.y FROM left_side l RIGHT JOIN right_side r ON r.k = l.k WHERE l.x IS NOT 'c' ORDER BY 1", "B\nD" );
		rows ( db, "SELECT count ( * ), count ( l.k ), count ( r.k ) FROM left_side l FULL JOIN right_side r ON r.k = l.k AND r.y <> 'CC'", "5|3|4" );
		rows ( db, "SELECT p.name, x.kind FROM pets x RIGHT JOIN people p ON p.id = x.owner WHERE p.id <= 3 ORDER BY p.id, x.kind", "Ada|cat\nAda|dog\nLinus|NULL\nGrace|parrot" );
		rows ( db, "SELECT a.k, b.k, c.k FROM left_side a RIGHT JOIN right_side b ON b.k = a.k RIGHT JOIN ( SELECT 4 AS k UNION ALL SELECT 5 ) AS c ON c.k = b.k ORDER BY c.k", "NULL|4|4\nNULL|NULL|5" );
		rows ( db, "SELECT y FROM left_side l RIGHT JOIN right_side r ON r.k = l.k ORDER BY y LIMIT 1 OFFSET 3", "D" );
		plan_has ( db, "EXPLAIN QUERY PLAN SELECT * FROM left_side l FULL JOIN right_side r ON r.k = l.k", "(FULL JOIN)" );
		fails ( db, "SELECT * FROM left_side NATURAL JOIN right_side USING ( k )", "cannot also have ON or USING" );
		rows ( db, "SELECT 1 UNION SELECT 1 UNION SELECT 2", "1\n2" );
		rows ( db, "SELECT 1 UNION SELECT 2 ORDER BY 1 DESC", "2\n1" );
		rows ( db, "SELECT 1 UNION ALL SELECT 1", "1\n1" );
		rows ( db, "SELECT city FROM people INTERSECT SELECT 'London'", "London" );
		rows ( db, "SELECT city FROM people EXCEPT SELECT 'London' ORDER BY 1", "Boston\nHelsinki\nNew York" );
		rows ( db, "SELECT k FROM left_side UNION SELECT k FROM right_side ORDER BY k LIMIT 3 OFFSET 1", "2\n3\n4" );
		rows ( db, "WITH big ( n ) AS ( SELECT age FROM people WHERE age > 40 ) SELECT count ( * ), sum ( n ) FROM big", "2|86" );
		rows ( db, "WITH a AS ( SELECT 1 AS x ), b AS ( SELECT x + 1 AS y FROM a ) SELECT x, y FROM a, b", "1|2" );
		rows ( db, "WITH RECURSIVE counter ( n ) AS ( SELECT 1 UNION ALL SELECT n + 1 FROM counter WHERE n < 5 ) SELECT group_concat ( n ) FROM counter", "1,2,3,4,5" );
		rows ( db, "WITH RECURSIVE fib ( a, b ) AS ( SELECT 0, 1 UNION ALL SELECT b, a + b FROM fib LIMIT 10 ) SELECT max ( a ) FROM fib", "34" );
		rows ( db, "WITH RECURSIVE walk ( n ) AS ( SELECT 1 UNION SELECT n % 4 + 1 FROM walk ) SELECT count ( * ) FROM walk", "4" );
		rows ( db, "SELECT name FROM people WHERE id IN ( WITH picked AS ( SELECT 3 AS id ) SELECT id FROM picked )", "Grace" );
		rows ( db, "WITH RECURSIVE nums ( n ) AS ( SELECT 1 UNION ALL SELECT n + 1 FROM nums WHERE n < 1000 ) SELECT count ( * ), sum ( n ) FROM nums", "1000|500500" );
		fails ( db, "WITH RECURSIVE bad ( n ) AS ( SELECT n FROM bad ) SELECT * FROM bad", "must start with a part" );
		rows ( db, "SELECT name, row_number ( ) OVER ( ORDER BY age DESC ) FROM people WHERE age IS NOT NULL ORDER BY 2", "Grace|1\nAlan|2\nAda|3\nLinus|4" );
		rows ( db, "SELECT name, city, rank ( ) OVER ( PARTITION BY city ORDER BY age ) FROM people WHERE city = 'London' ORDER BY 3", "Ada|London|1\nAlan|London|2" );
		rows ( db, "SELECT name, sum ( age ) OVER ( ORDER BY id ), count ( * ) OVER ( ) FROM people ORDER BY id", "Ada|36|5\nLinus|64|5\nGrace|109|5\nAlan|150|5\nBarbara|150|5" );
		rows ( db, "SELECT id, lag ( id ) OVER ( ORDER BY id ), lead ( id, 2, -1 ) OVER ( ORDER BY id ) FROM people ORDER BY id", "1|NULL|3\n2|1|4\n3|2|5\n4|3|-1\n5|4|-1" );
		rows ( db, "SELECT id, avg ( age ) OVER ( ORDER BY id ROWS BETWEEN 1 PRECEDING AND 1 FOLLOWING ) FROM people WHERE id < 4 ORDER BY id", "1|32.0\n2|36.3333333333333\n3|36.5" );
		rows ( db, "SELECT city, count ( * ), rank ( ) OVER ( ORDER BY count ( * ) DESC ) FROM people GROUP BY city ORDER BY 3, 1", "London|2|1\nBoston|1|2\nHelsinki|1|2\nNew York|1|2" );
		rows ( db, "SELECT dense_rank ( ) OVER ( ORDER BY city ), ntile ( 2 ) OVER ( ORDER BY id ), first_value ( name ) OVER ( PARTITION BY city ORDER BY id ), last_value ( name ) OVER ( ORDER BY id ROWS BETWEEN CURRENT ROW AND UNBOUNDED FOLLOWING ) FROM people ORDER BY id", "3|1|Ada|Barbara\n2|1|Linus|Barbara\n4|1|Grace|Barbara\n3|2|Ada|Barbara\n1|2|Barbara|Barbara" );
		rows ( db, "SELECT percent_rank ( ) OVER ( ORDER BY id ), cume_dist ( ) OVER ( ORDER BY id ), nth_value ( name, 2 ) OVER ( ORDER BY id ) FROM people WHERE id <= 2 ORDER BY id", "0.0|0.5|NULL\n1.0|1.0|Linus" );
		fails ( db, "SELECT row_number ( ) FROM people", "needs an OVER clause" );
		rows ( db, "SELECT json_group_object ( name, age ) FROM people WHERE id <= 2", "{\"Ada\":36,\"Linus\":28}" );
		fails ( db, "SELECT name FROM people WHERE rank ( ) OVER ( ) > 1", "can only be used in the result columns" );
		fails ( db, "WITH x ( a, b ) AS ( SELECT 1 ) SELECT * FROM x", "different number of columns" );

	}

	static void constraint_tests ( engine::database& db ) {

		fails ( db, "INSERT INTO people ( name ) VALUES ( NULL )", "NOT NULL constraint failed: people.name" );
		run ( db, "CREATE TABLE keyed ( a INT UNIQUE, b TEXT CHECK ( length ( b ) < 5 ) )" );
		run ( db, "INSERT INTO keyed VALUES ( 1, 'x' )" );
		fails ( db, "INSERT INTO keyed VALUES ( 1, 'y' )", "UNIQUE constraint failed: keyed.a" );
		fails ( db, "INSERT INTO keyed VALUES ( 2, 'toolong' )", "CHECK constraint failed: keyed" );
		run ( db, "INSERT OR IGNORE INTO keyed VALUES ( 1, 'z' )" );
		rows ( db, "SELECT count ( * ), changes ( ) FROM keyed", "1|0" );
		run ( db, "INSERT OR REPLACE INTO keyed VALUES ( 1, 'w' )" );
		rows ( db, "SELECT a, b FROM keyed", "1|w" );
		run ( db, "INSERT INTO keyed VALUES ( 1, 'v' ) ON CONFLICT ( a ) DO UPDATE SET b = excluded.b || '!'" );
		rows ( db, "SELECT a, b FROM keyed", "1|v!" );
		run ( db, "INSERT INTO keyed VALUES ( 1, 'q' ) ON CONFLICT DO NOTHING" );
		rows ( db, "SELECT b FROM keyed", "v!" );
		run ( db, "INSERT INTO keyed VALUES ( NULL, 'n1' ), ( NULL, 'n2' )" );
		rows ( db, "SELECT count ( * ) FROM keyed", "3" );
		fails ( db, "UPDATE keyed SET a = 1 WHERE b = 'n1'", "UNIQUE constraint failed" );
		fails ( db, "INSERT INTO keyed VALUES ( 5, 'a' ), ( 1, 'dup' )", "UNIQUE" );
		rows ( db, "SELECT count ( * ) FROM keyed WHERE a = 5", "0" );
		fails ( db, "INSERT INTO people ( id, name ) VALUES ( 1, 'Again' )", "UNIQUE constraint failed: people.id" );
		fails ( db, "INSERT INTO people ( id, name ) VALUES ( 'abc', 'Bad' )", "whole number" );
		run ( db, "CREATE TABLE pairs ( a INT, b INT, PRIMARY KEY ( a, b ) )" );
		run ( db, "INSERT INTO pairs VALUES ( 1, 1 ), ( 1, 2 )" );
		fails ( db, "INSERT INTO pairs VALUES ( 1, 2 )", "UNIQUE constraint failed: pairs.a, pairs.b" );
		fails ( db, "INSERT INTO pairs VALUES ( NULL, 2 )", "NOT NULL" );
		run ( db, "CREATE TABLE defaults ( id INTEGER PRIMARY KEY, a TEXT DEFAULT 'none', b INT DEFAULT ( 2 + 3 ), c REAL DEFAULT -1 )" );
		run ( db, "INSERT INTO defaults DEFAULT VALUES" );
		run ( db, "INSERT INTO defaults ( a ) VALUES ( 'set' )" );
		rows ( db, "SELECT * FROM defaults", "1|none|5|-1.0\n2|set|5|-1.0" );
		run ( db, "CREATE TABLE typed ( i INTEGER, r REAL, t TEXT, n NUMERIC, b BLOB )" );
		run ( db, "INSERT INTO typed VALUES ( '42', '1.5', 7, '3.0', 'raw' )" );
		rows ( db, "SELECT typeof ( i ), typeof ( r ), typeof ( t ), typeof ( n ), typeof ( b ), i, r, t, n FROM typed", "integer|real|text|integer|text|42|1.5|7|3" );
		fails ( db, "CREATE TABLE people ( x INT )", "already used" );
		run ( db, "CREATE TABLE IF NOT EXISTS people ( x INT )" );
		fails ( db, "CREATE TABLE strata_mine ( x INT )", "kept for Strata" );
		run ( db, "CREATE TABLE \"long rows\" ( \"the text\" TEXT, n INT )" );
		run ( db, "CREATE INDEX long_text ON \"long rows\" ( \"the text\" )" );
		run ( db, "INSERT INTO \"long rows\" VALUES ( 'short', 1 ), ( printf ( '%.20000c', 'z' ), 2 ), ( printf ( '%.9000c', 'a' ) || 'b', 3 )" );
		rows ( db, "SELECT n, length ( \"the text\" ) FROM \"long rows\" WHERE \"the text\" > 'b' ORDER BY n", "1|5\n2|20000" );
		rows ( db, "SELECT n FROM \"long rows\" WHERE \"the text\" = printf ( '%.20000c', 'z' )", "2" );
		run ( db, "UPDATE \"long rows\" SET \"the text\" = substr ( \"the text\", 1, 3 ) WHERE n = 2" );
		rows ( db, "SELECT n, \"the text\" FROM \"long rows\" AS r WHERE r.n = 2", "2|zzz" );
		run ( db, "DROP TABLE \"long rows\"" );

	}

	static void foreign_tests ( engine::database& db ) {

		run ( db, "CREATE INDEX pets_owner ON pets ( owner )" );
		fails ( db, "INSERT INTO pets ( owner, kind ) VALUES ( 99, 'ghost' )", "FOREIGN KEY constraint failed" );
		run ( db, "INSERT INTO pets ( owner, kind ) VALUES ( NULL, 'stray' )" );
		run ( db, "CREATE TABLE claims ( person INT REFERENCES people ( id ) )" );
		run ( db, "INSERT INTO claims VALUES ( 3 )" );
		fails ( db, "DELETE FROM people WHERE id = 3", "FOREIGN KEY constraint failed" );
		fails ( db, "UPDATE people SET id = 30 WHERE id = 3", "FOREIGN KEY constraint failed" );
		run ( db, "CREATE TABLE notes ( person INT REFERENCES people ( id ) ON DELETE SET NULL, body TEXT )" );
		run ( db, "INSERT INTO notes VALUES ( 4, 'hello' )" );
		run ( db, "DELETE FROM people WHERE id = 4" );
		rows ( db, "SELECT person, body FROM notes", "NULL|hello" );
		run ( db, "DELETE FROM people WHERE id = 1" );
		rows ( db, "SELECT kind FROM pets ORDER BY id", "parrot\nstray" );
		rows ( db, "SELECT changes ( )", "1" );
		fails ( db, "DROP TABLE people", "foreign key" );

	}

	static void index_tests ( engine::database& db ) {

		run ( db, "CREATE INDEX people_city ON people ( city )" );
		plan_has ( db, "EXPLAIN QUERY PLAN SELECT name FROM people WHERE city = 'London'", "USING INDEX people_city (city=?)" );
		plan_has ( db, "EXPLAIN QUERY PLAN SELECT name FROM people WHERE id = 2", "USING ROWID (rowid=?)" );
		plan_has ( db, "EXPLAIN QUERY PLAN SELECT name FROM people WHERE id > 2", "USING ROWID (rowid>?)" );
		plan_has ( db, "EXPLAIN QUERY PLAN SELECT name FROM people WHERE +city = 'London'", "SCAN people" );
		plan_has ( db, "EXPLAIN QUERY PLAN SELECT p.name FROM pets x JOIN people p ON p.id = x.owner", "SEARCH people AS p USING ROWID" );
		plan_has ( db, "EXPLAIN QUERY PLAN SELECT * FROM keyed WHERE a = 1", "USING UNIQUE INDEX strata_auto_keyed_1 (a=?)" );
		plan_has ( db, "EXPLAIN QUERY PLAN DELETE FROM people WHERE city = 'Boston'", "USING INDEX people_city" );
		plan_has ( db, "EXPLAIN QUERY PLAN SELECT ( SELECT 1 FROM pets WHERE owner = people.id ) FROM people", "CORRELATED SCALAR SUBQUERY" );
		rows ( db, "SELECT name FROM people WHERE city = 'London'", "" );
		rows ( db, "SELECT name FROM people WHERE city = 'Boston'", "Barbara" );
		rows ( db, "SELECT name FROM people WHERE city > 'C' ORDER BY name", "Grace\nLinus" );
		plan_has ( db, "EXPLAIN QUERY PLAN SELECT city, id FROM people WHERE city > 'C'", "USING COVERING INDEX people_city (city>?)" );
		plan_has ( db, "EXPLAIN QUERY PLAN SELECT count ( * ) FROM people WHERE city = 'Boston'", "USING COVERING INDEX people_city" );
		plan_lacks ( db, "EXPLAIN QUERY PLAN SELECT city, name FROM people WHERE city > 'C'", "COVERING" );
		plan_lacks ( db, "EXPLAIN QUERY PLAN SELECT city FROM people p WHERE city > 'C' AND EXISTS ( SELECT 1 FROM pets WHERE owner = p.age )", "COVERING INDEX people_city" );
		rows ( db, "SELECT city, id, rowid FROM people WHERE city > 'C' ORDER BY id", "Helsinki|2|2\nNew York|3|3" );
		rows ( db, "SELECT upper ( city ), count ( * ) FROM people WHERE city >= 'B' GROUP BY city ORDER BY 1", "BOSTON|1\nHELSINKI|1\nNEW YORK|1" );
		run ( db, "UPDATE people SET city = 'Paris' WHERE name = 'Grace'" );
		rows ( db, "SELECT name FROM people WHERE city = 'Paris'", "Grace" );
		rows ( db, "SELECT count ( * ) FROM people WHERE city = 'New York'", "0" );
		run ( db, "CREATE UNIQUE INDEX people_name ON people ( name )" );
		fails ( db, "INSERT INTO people ( name ) VALUES ( 'Grace' )", "UNIQUE constraint failed: people.name" );
		run ( db, "INSERT INTO pairs VALUES ( 2, 2 )" );
		fails ( db, "CREATE UNIQUE INDEX pairs_b ON pairs ( b )", "duplicates" );
		fails ( db, "DROP INDEX strata_auto_keyed_1", "cannot be dropped" );
		run ( db, "DROP INDEX people_name" );
		run ( db, "INSERT INTO people ( name ) VALUES ( 'Grace' )" );
		rows ( db, "SELECT count ( * ) FROM people WHERE name = 'Grace'", "2" );

	}

	static void transaction_tests ( engine::database& db ) {

		run ( db, "CREATE TABLE steps ( a INT UNIQUE )" );
		run ( db, "INSERT INTO steps VALUES ( 1 ), ( 3 ), ( 4 )" );
		run ( db, "BEGIN" );
		run ( db, "INSERT INTO steps VALUES ( 10 )" );
		fails ( db, "UPDATE steps SET a = a + 1", "UNIQUE" );
		rows ( db, "SELECT a FROM steps ORDER BY rowid", "1\n3\n4\n10" );
		fails ( db, "BEGIN", "already open" );
		run ( db, "ROLLBACK" );
		rows ( db, "SELECT count ( * ) FROM steps", "3" );
		run ( db, "BEGIN; INSERT INTO steps VALUES ( 11 ); CREATE TABLE inside ( x ); INSERT INTO inside VALUES ( 1 ); COMMIT" );
		rows ( db, "SELECT count ( * ) FROM steps; SELECT x FROM inside", "4\n1" );
		run ( db, "BEGIN; CREATE TABLE gone ( x ); ROLLBACK" );
		fails ( db, "SELECT * FROM gone", "no such table" );
		fails ( db, "COMMIT", "no open transaction" );

	}

	static void alter_tests ( engine::database& db ) {

		run ( db, "ALTER TABLE people ADD COLUMN email TEXT DEFAULT 'none'" );
		rows ( db, "SELECT email FROM people WHERE id = 2", "none" );
		run ( db, "UPDATE people SET email = 'linus@example.org' WHERE id = 2" );
		rows ( db, "SELECT name, email FROM people WHERE id IN ( 2, 3 ) ORDER BY id", "Linus|linus@example.org\nGrace|none" );
		run ( db, "ALTER TABLE people RENAME COLUMN city TO town" );
		rows ( db, "SELECT town FROM people WHERE id = 2", "Helsinki" );
		plan_has ( db, "EXPLAIN QUERY PLAN SELECT name FROM people WHERE town = 'Paris'", "USING INDEX people_city (town=?)" );
		run ( db, "ALTER TABLE keyed RENAME TO uniq" );
		rows ( db, "SELECT count ( * ) FROM uniq", "3" );
		fails ( db, "INSERT INTO uniq VALUES ( 1, 'x' )", "UNIQUE" );
		fails ( db, "ALTER TABLE people ADD COLUMN bad INT NOT NULL", "NOT NULL" );
		run ( db, "CREATE TABLE londoners AS SELECT name, age FROM people WHERE town = 'Helsinki'" );
		rows ( db, "SELECT * FROM londoners", "Linus|28" );
		run ( db, "DROP TABLE londoners" );
		fails ( db, "SELECT * FROM londoners", "no such table" );
		run ( db, "CREATE TABLE wide ( id INTEGER PRIMARY KEY, a TEXT, b INT CHECK ( b >= 0 ), c TEXT DEFAULT 'cee', d BLOB, e INT UNIQUE )" );
		run ( db, "INSERT INTO wide ( a, b, c, d, e ) VALUES ( 'x', 1, 'one', x'01', 10 ), ( 'y', 2, NULL, NULL, 20 ), ( NULL, 3, 'three', zeroblob ( 3000 ), 30 )" );
		run ( db, "CREATE INDEX wide_c ON wide ( c )" );
		run ( db, "ALTER TABLE wide ADD COLUMN f TEXT DEFAULT 'late'" );
		fails ( db, "ALTER TABLE wide DROP COLUMN id", "PRIMARY KEY" );
		fails ( db, "ALTER TABLE wide DROP COLUMN e", "UNIQUE" );
		fails ( db, "ALTER TABLE wide DROP COLUMN c", "used by index wide_c" );
		fails ( db, "ALTER TABLE wide DROP COLUMN nope", "has no column" );
		run ( db, "ALTER TABLE wide DROP COLUMN a" );
		rows ( db, "SELECT id, b, c, length ( d ), e, f FROM wide ORDER BY id", "1|1|one|1|10|late\n2|2|NULL|NULL|20|late\n3|3|three|3000|30|late" );
		fails ( db, "SELECT count ( * ) FROM wide WHERE a IS NULL", "no such column: a" );
		run ( db, "ALTER TABLE wide DROP COLUMN d" );
		rows ( db, "SELECT id, b, c, e, f FROM wide WHERE c = 'three'", "3|3|three|30|late" );
		plan_has ( db, "EXPLAIN QUERY PLAN SELECT id FROM wide WHERE c = 'one'", "USING COVERING INDEX wide_c" );
		fails ( db, "INSERT INTO wide ( b, c, e ) VALUES ( -1, 'neg', 40 )", "CHECK" );
		fails ( db, "INSERT INTO wide ( b, e ) VALUES ( 5, 10 )", "UNIQUE" );
		run ( db, "INSERT INTO wide ( b, e ) VALUES ( 5, 50 )" );
		rows ( db, "SELECT c, f FROM wide WHERE e = 50", "cee|late" );
		run ( db, "ALTER TABLE wide DROP COLUMN f" );
		rows ( db, "SELECT count ( * ), group_concat ( c ) FROM wide", "4|one,three,cee" );
		run ( db, "CREATE TABLE solo ( only_one )" );
		fails ( db, "ALTER TABLE solo DROP COLUMN only_one", "only column" );
		run ( db, "CREATE TABLE boxes ( id INTEGER PRIMARY KEY, w REAL, h REAL, area REAL GENERATED ALWAYS AS ( w * h ) STORED, label TEXT AS ( printf ( '%gx%g=%g', w, h, area ) ), big INT AS ( area > 10 ) )" );
		run ( db, "INSERT INTO boxes ( w, h ) VALUES ( 2, 3 ), ( 4, 5 )" );
		run ( db, "INSERT INTO boxes VALUES ( 10, 1.5, 2 )" );
		rows ( db, "SELECT id, area, label, big FROM boxes ORDER BY id", "1|6.0|2x3=6|0\n2|20.0|4x5=20|1\n10|3.0|1.5x2=3|0" );
		run ( db, "UPDATE boxes SET w = 10 WHERE id = 1" );
		rows ( db, "SELECT area, label, big FROM boxes WHERE id = 1", "30.0|10x3=30|1" );
		fails ( db, "INSERT INTO boxes ( w, h, area ) VALUES ( 1, 1, 5 )", "cannot INSERT into the generated column area" );
		fails ( db, "UPDATE boxes SET area = 1", "cannot UPDATE the generated column area" );
		fails ( db, "INSERT INTO boxes VALUES ( 11, 1, 1, 1 )", "takes 3 values" );
		run ( db, "CREATE INDEX boxes_area ON boxes ( area )" );
		rows ( db, "SELECT id FROM boxes WHERE area = 20", "2" );
		plan_has ( db, "EXPLAIN QUERY PLAN SELECT id FROM boxes WHERE area = 20", "USING COVERING INDEX boxes_area" );
		run ( db, "ALTER TABLE boxes ADD COLUMN perimeter REAL AS ( 2 * ( w + h ) )" );
		rows ( db, "SELECT id, perimeter FROM boxes ORDER BY id", "1|26.0\n2|18.0\n10|7.0" );
		run ( db, "UPDATE boxes SET h = 1 WHERE id = 2" );
		rows ( db, "SELECT perimeter, area FROM boxes WHERE id = 2", "10.0|4.0" );
		fails ( db, "ALTER TABLE boxes DROP COLUMN w", "used by a generated column" );
		run ( db, "ALTER TABLE boxes RENAME COLUMN w TO width" );
		run ( db, "INSERT INTO boxes ( width, h ) VALUES ( 3, 3 )" );
		rows ( db, "SELECT area, perimeter FROM boxes WHERE id = 11", "9.0|12.0" );
		fails ( db, "CREATE TABLE loop ( a AS ( b + 1 ), b AS ( a + 1 ) )", "depends on itself" );
		fails ( db, "CREATE TABLE sub ( a, b AS ( ( SELECT 1 ) ) )", "cannot use a subquery" );
		run ( db, "CREATE TABLE chain ( a INT, c AS ( b * 2 ), b AS ( a + 1 ) )" );
		run ( db, "INSERT INTO chain VALUES ( 4 )" );
		rows ( db, "SELECT a, b, c FROM chain", "4|5|10" );

	}

	static void collation_tests ( engine::database& db ) {

		rows ( db, "SELECT 'abc' = 'ABC', 'abc' = 'ABC' COLLATE NOCASE, 'abc' COLLATE NOCASE < 'ABD', 'b' COLLATE BINARY > 'A'", "0|1|1|1" );
		run ( db, "CREATE TABLE tags ( id INTEGER PRIMARY KEY, label TEXT COLLATE NOCASE, raw TEXT )" );
		run ( db, "INSERT INTO tags ( label, raw ) VALUES ( 'beta', 'beta' ), ( 'Alpha', 'Alpha' ), ( 'ALPHA', 'ALPHA' ), ( 'gamma', 'gamma' ), ( 'Beta', 'Beta' )" );
		rows ( db, "SELECT count ( * ) FROM tags WHERE label = 'alpha'", "2" );
		rows ( db, "SELECT count ( * ) FROM tags WHERE raw = 'alpha'", "0" );
		rows ( db, "SELECT count ( * ) FROM tags WHERE raw = 'alpha' COLLATE NOCASE", "2" );
		rows ( db, "SELECT raw FROM tags ORDER BY raw, id", "ALPHA\nAlpha\nBeta\nbeta\ngamma" );
		rows ( db, "SELECT raw FROM tags ORDER BY raw COLLATE NOCASE, id", "Alpha\nALPHA\nbeta\nBeta\ngamma" );
		rows ( db, "SELECT label FROM tags ORDER BY label, id", "Alpha\nALPHA\nbeta\nBeta\ngamma" );
		rows ( db, "SELECT count ( * ) FROM ( SELECT DISTINCT label FROM tags ) AS d", "3" );
		rows ( db, "SELECT count ( * ) FROM ( SELECT DISTINCT raw FROM tags ) AS d", "5" );
		rows ( db, "SELECT count ( * ) FROM tags GROUP BY label ORDER BY 1", "1\n2\n2" );
		rows ( db, "SELECT count ( * ) FROM tags GROUP BY raw COLLATE NOCASE ORDER BY 1", "1\n2\n2" );
		run ( db, "CREATE INDEX tags_label ON tags ( label )" );
		rows ( db, "SELECT count ( * ) FROM tags WHERE label = 'BETA'", "2" );
		rows ( db, "SELECT raw FROM tags WHERE label > 'b' ORDER BY id", "beta\ngamma\nBeta" );
		fails ( db, "SELECT 1 COLLATE klingon", "only the BINARY and NOCASE collations" );

	}

	static void persistence_tests ( engine::database& db, const char* path ) {

		rows ( db, "SELECT count ( * ), sum ( age ) FROM people", "4|73" );
		engine::close_database ( db );
		expect ( engine::open_database ( db, path ), "the database opens again" );
		rows ( db, "SELECT count ( * ), sum ( age ) FROM people", "4|73" );
		rows ( db, "SELECT name, town, email FROM people WHERE id = 2", "Linus|Helsinki|linus@example.org" );
		rows ( db, "SELECT count ( * ) FROM uniq", "3" );
		rows ( db, "SELECT count ( * ) FROM tags WHERE label = 'GAMMA'", "1" );
		plan_has ( db, "EXPLAIN QUERY PLAN SELECT * FROM people WHERE town = 'x'", "people_city" );
		rows ( db, "SELECT name FROM people ORDER BY town LIMIT 2", "Grace\nBarbara" );
		plan_has ( db, "EXPLAIN QUERY PLAN SELECT name FROM people ORDER BY town LIMIT 2", "USING INDEX people_city" );
		plan_lacks ( db, "EXPLAIN QUERY PLAN SELECT name FROM people ORDER BY town LIMIT 2", "SORT" );
		plan_lacks ( db, "EXPLAIN QUERY PLAN SELECT id FROM people ORDER BY id", "SORT" );
		plan_has ( db, "EXPLAIN QUERY PLAN SELECT id FROM people ORDER BY name", "SORT" );
		rows ( db, "SELECT id FROM people ORDER BY id LIMIT 2", "2\n3" );
		rows ( db, "SELECT id FROM people WHERE town = 'Helsinki' ORDER BY id", "2" );

	}

	static void model_where ( base::text& sql, base::text& forced, const fuzz_row* model, uint32_t size, test_random& random, base::text& wanted ) {

		static const char* const letters [ 3 ] = { "p", "q", "r" };
		uint32_t shape = random.below ( 9 );
		int64_t k = random.below ( 32 );
		int64_t m = k + random.below ( 10 );
		int32_t letter = static_cast < int32_t > ( random.below ( 3 ) );
		base::text where;
		base::text plain;

		switch ( shape ) {

			case 0: where.add ( "a = " ).signed_number ( k ); plain.add ( "+a = " ).signed_number ( k ); break;
			case 1: where.add ( "a > " ).signed_number ( k ); plain.add ( "+a > " ).signed_number ( k ); break;
			case 2: where.add ( "a >= " ).signed_number ( k ).add ( " AND a < " ).signed_number ( m ); plain.add ( "+a >= " ).signed_number ( k ).add ( " AND +a < " ).signed_number ( m ); break;
			case 3: where.add ( "a BETWEEN " ).signed_number ( k ).add ( " AND " ).signed_number ( m ); plain.add ( "+a BETWEEN " ).signed_number ( k ).add ( " AND " ).signed_number ( m ); break;
			case 4: where.add ( "a = " ).signed_number ( k ).add ( " AND b = '" ).add ( letters [ letter ] ).add ( '\'' ); plain.add ( "+a = " ).signed_number ( k ).add ( " AND +b = '" ).add ( letters [ letter ] ).add ( '\'' ); break;
			case 5: where.add ( "a IN ( " ).signed_number ( k ).add ( ", " ).signed_number ( m ).add ( " )" ); plain.add ( "+a IN ( " ).signed_number ( k ).add ( ", " ).signed_number ( m ).add ( " )" ); break;
			case 6: where.add ( "id > " ).signed_number ( k * 15 ); plain.add ( "+id > " ).signed_number ( k * 15 ); break;
			case 7: where.add ( "id BETWEEN " ).signed_number ( k * 10 ).add ( " AND " ).signed_number ( m * 20 ); plain.add ( "+id BETWEEN " ).signed_number ( k * 10 ).add ( " AND " ).signed_number ( m * 20 ); break;
			default: where.add ( "a IS NULL AND id < " ).signed_number ( m * 30 ); plain.add ( "+a IS NULL AND +id < " ).signed_number ( m * 30 ); break;

		}

		sql.clear ( );
		sql.add ( "SELECT id FROM fuzz WHERE " ).add ( where.value ( ) ).add ( " ORDER BY id" );
		forced.clear ( );
		forced.add ( "SELECT id FROM fuzz WHERE " ).add ( plain.value ( ) ).add ( " ORDER BY id" );
		wanted.clear ( );

		bool first = true;

		for ( uint32_t id = 1; id < size; id++ ) {

			const fuzz_row& row = model [ id ];
			bool hit = false;

			if ( !row.present )
				continue;

			switch ( shape ) {

				case 0: hit = !row.a_null && row.a == k; break;
				case 1: hit = !row.a_null && row.a > k; break;
				case 2: hit = !row.a_null && row.a >= k && row.a < m; break;
				case 3: hit = !row.a_null && row.a >= k && row.a <= m; break;
				case 4: hit = !row.a_null && row.a == k && row.b == letter; break;
				case 5: hit = !row.a_null && ( row.a == k || row.a == m ); break;
				case 6: hit = id > k * 15; break;
				case 7: hit = id >= k * 10 && id <= m * 20; break;
				default: hit = row.a_null && id < m * 30; break;

			}

			if ( !hit )
				continue;

			if ( !first )
				wanted.add ( '\n' );

			wanted.number ( id );
			first = false;

		}

	}

	static void fuzz_tests ( engine::database& db ) {

		static const uint32_t size = 400;
		static const char* const letters [ 3 ] = { "p", "q", "r" };
		fuzz_row* model = base::make < fuzz_row > ( size * 2 );
		fuzz_row* saved = model + size;
		test_random random = { 0xD1B54A32D192ED03ull };
		base::text sql;
		bool fine = model != nullptr;

		run ( db, "CREATE TABLE fuzz ( id INTEGER PRIMARY KEY, a INT, b TEXT, c REAL )" );
		run ( db, "CREATE INDEX fuzz_a ON fuzz ( a )" );
		run ( db, "CREATE INDEX fuzz_ab ON fuzz ( a, b )" );

		uint32_t mismatches = 0;
		uint32_t queries = 0;

		for ( uint32_t round = 0; round < 60 && fine; round++ ) {

			bool rollback = random.below ( 5 ) == 0;

			base::copy ( saved, model, sizeof ( fuzz_row ) * size );
			fine = engine::execute ( db, "BEGIN", nullptr, 0, nullptr );

			for ( uint32_t step = 0; step < 60 && fine; step++ ) {

				uint32_t id = 1 + random.below ( size - 1 );
				uint32_t action = random.below ( 10 );
				fuzz_row& row = model [ id ];

				sql.clear ( );

				if ( action < 6 ) {

					bool a_null = random.below ( 10 ) == 0;
					int64_t a = random.below ( 32 );
					int32_t b = random.below ( 4 ) == 3 ? -1 : static_cast < int32_t > ( random.below ( 3 ) );

					sql.add ( "INSERT OR REPLACE INTO fuzz ( id, a, b, c ) VALUES ( " ).number ( id ).add ( ", " );

					if ( a_null )
						sql.add ( "NULL" );
					else
						sql.signed_number ( a );

					sql.add ( ", " );

					if ( b < 0 )
						sql.add ( "NULL" );
					else
						sql.add ( '\'' ).add ( letters [ b ] ).add ( '\'' );

					sql.add ( ", " ).number ( id ).add ( " * 0.5 )" );
					row = { true, a_null, a, b };

				} else if ( action < 8 ) {

					sql.add ( "DELETE FROM fuzz WHERE id = " ).number ( id );
					row.present = false;

				} else if ( action < 9 ) {

					int64_t a = random.below ( 32 );

					sql.add ( "UPDATE fuzz SET a = " ).signed_number ( a ).add ( " WHERE id = " ).number ( id );

					if ( row.present ) {

						row.a = a;
						row.a_null = false;

					}

				} else {

					int64_t a = random.below ( 32 );

					sql.add ( "DELETE FROM fuzz WHERE a = " ).signed_number ( a );

					for ( uint32_t k = 1; k < size; k++ )
						if ( model [ k ].present && !model [ k ].a_null && model [ k ].a == a )
							model [ k ].present = false;

				}

				fine = engine::execute ( db, sql.value ( ), nullptr, 0, nullptr );

				if ( !fine ) {

					base::text line;

					line.add ( sql.value ( ) ).add ( ": " ).add ( db.problem.value ( ) );
					expect ( false, line.value ( ) );

				}

			}

			if ( fine && rollback ) {

				fine = engine::execute ( db, "ROLLBACK", nullptr, 0, nullptr );
				base::copy ( model, saved, sizeof ( fuzz_row ) * size );

			} else if ( fine )
				fine = engine::execute ( db, "COMMIT", nullptr, 0, nullptr );

			for ( uint32_t q = 0; q < 12 && fine; q++ ) {

				base::text indexed;
				base::text forced;
				base::text wanted;
				result_capture fast;
				result_capture slow;

				model_where ( indexed, forced, model, size, random, wanted );
				queries++;

				if ( !query ( db, indexed.value ( ), fast ) || !query ( db, forced.value ( ), slow ) ) {

					expect ( false, db.problem.value ( ) );
					fine = false;

					break;

				}

				if ( !base::same ( fast.out.value ( ), wanted.value ( ) ) || !base::same ( slow.out.value ( ), wanted.value ( ) ) ) {

					if ( mismatches++ < 5 ) {

						base::text line;

						line.add ( indexed.value ( ) ).add ( "\n          indexed: " ).add ( fast.out.value ( ) ).add ( "\n          scanned: " ).add ( slow.out.value ( ) ).add ( "\n          model:   " ).add ( wanted.value ( ) );
						expect ( false, line.value ( ) );

					}

				}

			}

		}

		expect ( fine && !mismatches, "random changes and lookups agree with the model through indexes and scans" );

		uint64_t live = 0;

		for ( uint32_t k = 1; k < size; k++ )
			live += model [ k ].present;

		for ( uint32_t i = 0; i < db.schema->index_count; i++ ) {

			engine::index_info& index = db.schema->indexes [ i ];

			if ( !base::same ( index.table, "fuzz" ) )
				continue;

			storage::tree_report report;
			bool intact = storage::check_btree ( db.pages, index.root, true, report );

			expect ( intact && report.entries == live, "every fuzz index holds exactly one entry per row" );

		}

		base::text summary;

		summary.number ( queries ).add ( " fuzz queries checked against the model" );
		expect ( queries > 500, summary.value ( ) );
		base::release ( model );

	}

	void engine_tests ( ) {

		section ( "sql engine" );

		char path [ FILE_PATH_ROOM ];

		if ( !expect ( scratch_path ( path, sizeof ( path ), "strata-test-engine.sdb" ), "a scratch path exists" ) )
			return;

		forget_database ( path );

		engine::database* db = base::make < engine::database > ( 1 );

		if ( !expect ( db && engine::open_database ( *db, path ), "a fresh database opens" ) )
			return;

		value_tests ( *db );
		table_tests ( *db );
		join_tests ( *db );
		constraint_tests ( *db );
		foreign_tests ( *db );
		index_tests ( *db );
		transaction_tests ( *db );
		alter_tests ( *db );
		collation_tests ( *db );
		persistence_tests ( *db, path );
		savepoint_tests ( *db, path );
		join_fuzz ( *db );
		join_order_tests ( *db );
		join_order_fuzz ( *db );
		vacuum_tests ( *db, path );
		search_tests ( *db );
		check_tests ( *db );
		prepared_tests ( *db );
		limit_tests ( *db );
		fuzz_tests ( *db );
		engine::close_database ( *db );
		db->problem.~text ( );
		base::release ( db );
		forget_database ( path );

	}

}
