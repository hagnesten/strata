#include "test/engine/checks.hh"
#include "engine/database/prepared.hh"

namespace test {

	static bool prepared_row ( void* context, const storage::value* values, uint32_t count ) {

		result_capture& capture = *static_cast < result_capture* > ( context );

		if ( capture.rows )
			capture.out.add ( '\n' );

		for ( uint32_t i = 0; i < count; i++ ) {

			if ( i )
				capture.out.add ( '|' );

			engine::describe_value ( capture.out, values [ i ] );

		}

		capture.rows++;

		return true;

	}

	static bool prepared_columns ( void* context, const char* const* names, uint32_t count ) {

		static_cast < result_capture* > ( context )->columns = count;

		return true;

	}

	static void prepared_rows ( engine::prepared_statement* item, const storage::value* values, uint32_t count, const char* what, const char* wanted ) {

		result_capture capture;
		engine::query_sink sink = { &capture, prepared_columns, prepared_row };

		capture.rows = 0;
		capture.columns = 0;

		if ( !item || !engine::run_prepared ( *item, values, count, &sink ) ) {

			base::text line;

			line.add ( what ).add ( "\n          error: " ).add ( item ? item->owner->problem.value ( ) : "not prepared" );
			expect ( false, line.value ( ) );

			return;

		}

		base::text line;

		line.add ( what ).add ( "\n          got:    " ).add ( capture.out.value ( ) ).add ( "\n          wanted: " ).add ( wanted );
		expect ( base::same ( capture.out.value ( ), wanted ), line.value ( ) );

	}

	static engine::prepared_statement* prepare ( engine::database& db, const char* sql ) {

		engine::prepared_statement* made = nullptr;
		base::text line;

		line.add ( "prepare " ).add ( sql ).add ( "\n          error: " ).add ( db.problem.value ( ) );

		bool fine = engine::prepare_statement ( db, sql, made, nullptr );

		if ( !fine ) {

			line.clear ( );
			line.add ( "prepare " ).add ( sql ).add ( "\n          error: " ).add ( db.problem.value ( ) );

		}

		expect ( fine && made, line.value ( ) );

		return made;

	}

	static void insert_runs ( engine::database& db ) {

		run ( db, "CREATE TABLE prepared_items ( id INTEGER PRIMARY KEY, name TEXT UNIQUE, amount REAL, tag TEXT )" );
		run ( db, "CREATE INDEX prepared_tags ON prepared_items ( tag )" );

		engine::prepared_statement* insert = prepare ( db, "INSERT INTO prepared_items ( name, amount, tag ) VALUES ( ?1, ?2, ?3 )" );
		bool fine = insert != nullptr;
		char name [ 16 ];
		static const char* const tags [ 3 ] = { "red", "green", "blue" };

		expect ( insert && engine::statement_parameters ( *insert ) == 3, "an insert with three parameters reports three" );
		run ( db, "BEGIN" );

		for ( uint32_t i = 0; insert && i < 500; i++ ) {

			uint32_t size = 0;

			name [ size++ ] = 'n';

			for ( uint32_t left = i + 1000; left; left /= 10 )
				name [ size++ ] = static_cast < char > ( '0' + left % 10 );

			storage::value values [ 3 ] = { storage::text_value ( name, size ), storage::real_value ( i * 0.5 ), storage::text_value ( tags [ i % 3 ], base::length ( tags [ i % 3 ] ) ) };

			if ( !engine::run_prepared ( *insert, values, 3, nullptr ) )
				fine = false;

		}

		run ( db, "COMMIT" );
		expect ( fine, "a prepared insert runs 500 times" );
		rows ( db, "SELECT count ( * ), sum ( amount ), count ( DISTINCT tag ) FROM prepared_items", "500|62375.0|3" );

		storage::value repeat [ 3 ] = { storage::text_value ( "n0001", 5 ), storage::real_value ( 1 ), storage::null_value ( ) };

		expect ( insert && !engine::run_prepared ( *insert, repeat, 3, nullptr ), "a prepared insert reports a UNIQUE clash" );
		expect ( contains ( db.problem.value ( ), "UNIQUE" ), "the clash names the UNIQUE constraint" );

		storage::value fresh [ 3 ] = { storage::text_value ( "fresh", 5 ), storage::real_value ( 2 ), storage::text_value ( "red", 3 ) };

		expect ( insert && engine::run_prepared ( *insert, fresh, 3, nullptr ), "a prepared insert runs again after a failure" );
		rows ( db, "SELECT count ( * ) FROM prepared_items", "501" );
		engine::finish_statement ( insert );

	}

	static void select_runs ( engine::database& db ) {

		engine::prepared_statement* pick = prepare ( db, "SELECT name, amount FROM prepared_items WHERE id = ?" );
		storage::value first = storage::integer_value ( 1 );
		storage::value tenth = storage::integer_value ( 10 );
		storage::value missing = storage::integer_value ( 99999 );

		prepared_rows ( pick, &first, 1, "a prepared lookup finds row 1", "n0001|0.0" );
		prepared_rows ( pick, &tenth, 1, "the same lookup finds row 10", "n9001|4.5" );
		prepared_rows ( pick, &missing, 1, "the same lookup finds nothing", "" );
		prepared_rows ( pick, &first, 1, "and finds row 1 again", "n0001|0.0" );
		engine::finish_statement ( pick );

		engine::prepared_statement* inner = prepare ( db, "SELECT count ( * ) FROM prepared_items WHERE id IN ( SELECT id FROM prepared_items WHERE tag = ?1 ) AND amount < ( SELECT avg ( amount ) FROM prepared_items WHERE tag = ?1 )" );
		storage::value red = storage::text_value ( "red", 3 );
		storage::value blue = storage::text_value ( "blue", 4 );
		storage::value none = storage::text_value ( "none", 4 );

		prepared_rows ( inner, &red, 1, "uncorrelated subqueries use the red parameter", "84" );
		prepared_rows ( inner, &blue, 1, "uncorrelated subqueries are not reused across runs", "83" );
		prepared_rows ( inner, &none, 1, "an empty subquery result", "0" );
		prepared_rows ( inner, &red, 1, "the red result comes back", "84" );
		engine::finish_statement ( inner );

		engine::prepared_statement* grouped = prepare ( db, "SELECT tag, count ( * ) FROM prepared_items WHERE amount >= ? GROUP BY tag ORDER BY tag" );
		storage::value low = storage::real_value ( 0 );
		storage::value high = storage::real_value ( 240 );

		prepared_rows ( grouped, &low, 1, "a prepared GROUP BY", "blue|166\ngreen|167\nred|168" );
		prepared_rows ( grouped, &high, 1, "the GROUP BY with a new bound", "blue|6\ngreen|7\nred|7" );
		prepared_rows ( grouped, &low, 1, "the GROUP BY with the first bound", "blue|166\ngreen|167\nred|168" );
		engine::finish_statement ( grouped );

	}

	static void change_runs ( engine::database& db ) {

		engine::prepared_statement* bump = prepare ( db, "UPDATE prepared_items SET amount = amount + ?2 WHERE tag = ?1" );
		storage::value values [ 2 ] = { storage::text_value ( "green", 5 ), storage::real_value ( 1000 ) };

		expect ( bump && engine::run_prepared ( *bump, values, 2, nullptr ), "a prepared update runs" );
		expect ( db.changes == 167, "the update changed 167 rows" );
		values [ 0 ] = storage::text_value ( "blue", 4 );
		values [ 1 ] = storage::real_value ( -1 );
		expect ( bump && engine::run_prepared ( *bump, values, 2, nullptr ), "the update runs with new parameters" );
		expect ( db.changes == 166, "the second update changed 166 rows" );
		rows ( db, "SELECT tag, count ( * ) FROM prepared_items WHERE amount >= 1000 GROUP BY tag", "green|167" );
		engine::finish_statement ( bump );

		engine::prepared_statement* drop = prepare ( db, "DELETE FROM prepared_items WHERE id > ?" );
		storage::value from = storage::integer_value ( 400 );

		expect ( drop && engine::run_prepared ( *drop, &from, 1, nullptr ), "a prepared delete runs" );
		expect ( db.changes == 101, "the delete removed 101 rows" );
		from = storage::integer_value ( 300 );
		expect ( drop && engine::run_prepared ( *drop, &from, 1, nullptr ), "the delete runs again" );
		expect ( db.changes == 100, "the second delete removed 100 rows" );
		rows ( db, "SELECT count ( * ), max ( id ) FROM prepared_items", "300|300" );
		engine::finish_statement ( drop );

	}

	static void schema_runs ( engine::database& db ) {

		engine::prepared_statement* all = prepare ( db, "SELECT * FROM prepared_items WHERE id = 2" );

		prepared_rows ( all, nullptr, 0, "SELECT * before the schema changes", "2|n1001|1000.5|green" );
		run ( db, "ALTER TABLE prepared_items ADD COLUMN note TEXT DEFAULT 'x'" );
		prepared_rows ( all, nullptr, 0, "SELECT * prepares again after ALTER TABLE", "2|n1001|1000.5|green|x" );
		run ( db, "CREATE TABLE prepared_other ( a )" );
		prepared_rows ( all, nullptr, 0, "SELECT * still runs after another table appears", "2|n1001|1000.5|green|x" );
		run ( db, "DROP TABLE prepared_items" );

		result_capture capture;
		engine::query_sink sink = { &capture, prepared_columns, prepared_row };

		capture.rows = 0;
		capture.columns = 0;
		expect ( all && !engine::run_prepared ( *all, nullptr, 0, &sink ), "a prepared SELECT fails once its table is gone" );
		expect ( contains ( db.problem.value ( ), "no such table" ), "the failure names the missing table" );
		run ( db, "CREATE TABLE prepared_items ( id INTEGER PRIMARY KEY, word TEXT )" );
		run ( db, "INSERT INTO prepared_items VALUES ( 2, 'back' )" );
		prepared_rows ( all, nullptr, 0, "the SELECT finds the new table", "2|back" );
		engine::finish_statement ( all );

		size_t used = 0;
		engine::prepared_statement* empty = nullptr;

		expect ( engine::prepare_statement ( db, "  -- nothing\n", empty, &used ) && !empty, "an empty text prepares to nothing" );

		engine::prepared_statement* first = nullptr;
		engine::prepared_statement* second = nullptr;
		const char* both = "SELECT 1; SELECT 2";

		expect ( engine::prepare_statement ( db, both, first, &used ) && first && used > 0 && used < 18, "prepare stops after the first statement" );
		prepared_rows ( first, nullptr, 0, "the first statement runs", "1" );
		expect ( engine::prepare_statement ( db, both + used, second, nullptr ) && second, "the tail prepares as the next statement" );
		prepared_rows ( second, nullptr, 0, "the second statement runs", "2" );
		engine::finish_statement ( first );
		engine::finish_statement ( second );

		engine::prepared_statement* broken = nullptr;

		expect ( !engine::prepare_statement ( db, "SELEC 1", broken, nullptr ) && !broken && db.parse_failed, "a syntax error fails to prepare" );
		run ( db, "DROP TABLE prepared_items" );
		run ( db, "DROP TABLE prepared_other" );

	}

	static void statement_undo ( engine::database& db ) {

		run ( db, "CREATE TABLE undo_items ( id INTEGER PRIMARY KEY, b TEXT UNIQUE, n INTEGER NOT NULL )" );
		run ( db, "CREATE INDEX undo_n ON undo_items ( n )" );
		run ( db, "BEGIN" );
		run ( db, "INSERT INTO undo_items VALUES ( 1, 'a', 10 )" );
		fails ( db, "INSERT INTO undo_items VALUES ( 2, 'a', 20 )", "UNIQUE" );
		expect ( db.in_transaction, "a failed single-row insert keeps the transaction open" );
		fails ( db, "INSERT INTO undo_items VALUES ( 2, 'x', NULL )", "NOT NULL" );
		run ( db, "INSERT INTO undo_items VALUES ( 3, 'c', 30 )" );
		fails ( db, "INSERT INTO undo_items VALUES ( 4, 'd', 40 ), ( 5, 'e', 50 ), ( 6, 'a', 60 )", "UNIQUE" );
		expect ( db.in_transaction, "a failed multi-row insert keeps the transaction open" );
		rows ( db, "SELECT id, b, n FROM undo_items ORDER BY id", "1|a|10\n3|c|30" );
		fails ( db, "UPDATE undo_items SET b = 'c' WHERE id = 1", "UNIQUE" );
		fails ( db, "UPDATE undo_items SET n = NULL WHERE id = 3", "NOT NULL" );
		run ( db, "UPDATE undo_items SET n = n + 1 WHERE id = 1" );
		fails ( db, "UPDATE undo_items SET b = 'z' WHERE id > 0", "UNIQUE" );
		rows ( db, "SELECT id, b, n FROM undo_items ORDER BY id", "1|a|11\n3|c|30" );
		rows ( db, "SELECT id FROM undo_items WHERE n = 11", "1" );
		run ( db, "SAVEPOINT inner_step" );
		run ( db, "DELETE FROM undo_items WHERE id = 3" );
		run ( db, "INSERT INTO undo_items VALUES ( 7, 'g', 70 )" );
		run ( db, "ROLLBACK TO inner_step" );
		rows ( db, "SELECT id, b, n FROM undo_items ORDER BY id", "1|a|11\n3|c|30" );
		run ( db, "RELEASE inner_step" );
		run ( db, "COMMIT" );
		rows ( db, "SELECT id, b, n FROM undo_items ORDER BY id", "1|a|11\n3|c|30" );
		rows ( db, "SELECT count ( * ) FROM undo_items WHERE n BETWEEN 0 AND 100", "2" );
		run ( db, "DROP TABLE undo_items" );

	}

	void prepared_tests ( engine::database& db ) {

		section ( "prepared statements" );
		insert_runs ( db );
		select_runs ( db );
		change_runs ( db );
		schema_runs ( db );
		statement_undo ( db );

	}

}
