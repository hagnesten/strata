#include "test/harness/harness.hh"
#include "strata.h"

namespace test {

	static int64_t single ( strata_db* db, const char* sql ) {

		strata_stmt* stmt = nullptr;
		int64_t out = -1;

		if ( strata_prepare ( db, sql, -1, &stmt, nullptr ) == STRATA_OK && strata_step ( stmt ) == STRATA_ROW )
			out = strata_column_int64 ( stmt, 0 );

		strata_finalize ( stmt );

		return out;

	}

	static void bound_inserts ( strata_db* db ) {

		strata_stmt* insert = nullptr;

		expect ( strata_prepare ( db, "INSERT INTO ticks ( symbol, price, size, note ) VALUES ( :symbol, :price, :size, :symbol || '!' )", -1, &insert, nullptr ) == STRATA_OK, "an insert with named parameters prepares" );
		expect ( strata_parameter_count ( insert ) == 3, "a repeated name shares one parameter" );
		expect ( strata_parameter_index ( insert, ":price" ) == 2 && strata_parameter_index ( insert, "size" ) == 3 && strata_parameter_index ( insert, ":none" ) == 0, "parameters are found by name" );
		strata_bind_text ( insert, 1, "AAPL", -1 );
		strata_bind_double ( insert, 2, 189.25 );
		strata_bind_int64 ( insert, 3, 100 );
		expect ( strata_step ( insert ) == STRATA_DONE, "stepping an insert finishes it" );
		expect ( strata_last_rowid ( db ) == 1 && strata_changes ( db ) == 1, "the insert reports its row id and change count" );
		expect ( strata_bind_int64 ( insert, 4, 1 ) == STRATA_RANGE, "binding past the last parameter is a range error" );
		strata_bind_int64 ( insert, 3, 100 );
		expect ( strata_step ( insert ) == STRATA_CONSTRAINT, "a duplicate key is a constraint error" );

		const char* problem = strata_error ( db );
		bool named = false;

		for ( size_t i = 0; problem [ i ]; i++ )
			if ( base::same_bytes ( problem + i, "UNIQUE", 6 ) )
				named = true;

		expect ( named, "the constraint error names UNIQUE" );
		strata_finalize ( insert );

	}

	static void batch_inserts ( strata_db* db ) {

		strata_stmt* insert = nullptr;
		strata_value* values = base::make < strata_value > ( 3 * 1000 );
		static const uint16_t wide [ 7 ] = { 'Z', 0xFC, 'r', 'i', 'c', 'h', 0x20AC };
		int done = 0;

		strata_prepare ( db, "INSERT INTO ticks ( symbol, price, size ) VALUES ( ?, ?, ? )", -1, &insert, nullptr );

		for ( int r = 0; values && r < 1000; r++ ) {

			strata_value* row = values + r * 3;

			if ( r % 2 ) {

				row [ 0 ].type = STRATA_TEXT16;
				row [ 0 ].data.bytes = wide;
				row [ 0 ].size = sizeof ( wide );

			} else {

				row [ 0 ].type = STRATA_TEXT;
				row [ 0 ].data.bytes = "MSFT";
				row [ 0 ].size = 4;

			}

			row [ 1 ].type = STRATA_REAL;
			row [ 1 ].data.real = 400 + r * 0.25;
			row [ 2 ].type = STRATA_INTEGER;
			row [ 2 ].data.integer = r + 1;

		}

		expect ( values && strata_run_batch ( insert, values, 1000, 3, &done ) == STRATA_OK && done == 1000, "a batch inserts 1000 rows in one call" );
		expect ( single ( db, "SELECT count ( * ) FROM ticks" ) == 1001, "the batch rows are all there" );
		expect ( single ( db, "SELECT count ( * ) FROM ticks WHERE symbol = 'Zürich€'" ) == 500, "UTF-16 text arrives as UTF-8" );
		values [ 2 ].data.integer = 3001;
		values [ 5 ] = values [ 2 ];
		values [ 3 ] = values [ 0 ];
		values [ 4 ] = values [ 1 ];
		expect ( strata_run_batch ( insert, values, 2, 3, &done ) == STRATA_CONSTRAINT && done == 1, "a batch stops at the first failing row" );
		strata_exec ( db, "BEGIN" );
		values [ 2 ].data.integer = 5000;
		strata_run_batch ( insert, values, 1, 3, &done );
		strata_exec ( db, "ROLLBACK" );
		expect ( single ( db, "SELECT count ( * ) FROM ticks" ) == 1002, "a rolled back batch leaves no rows" );
		base::release ( values );
		strata_finalize ( insert );

	}

	static void stepping ( strata_db* db ) {

		strata_stmt* scan = nullptr;
		strata_stmt* inner = nullptr;

		expect ( strata_prepare ( db, "SELECT id, symbol, price FROM ticks ORDER BY id", -1, &scan, nullptr ) == STRATA_OK, "a select prepares" );
		expect ( strata_column_count ( scan ) == 3 && base::same ( strata_column_name ( scan, 1 ), "symbol" ), "columns are known before the first step" );
		strata_prepare ( db, "SELECT count ( * ) FROM ticks WHERE id <= ?", -1, &inner, nullptr );

		int rows = 0;
		bool nested = true;
		int64_t last = 0;

		while ( strata_step ( scan ) == STRATA_ROW ) {

			int64_t id = strata_column_int64 ( scan, 0 );

			if ( id <= last )
				nested = false;

			last = id;
			rows++;

			if ( rows % 97 == 0 ) {

				strata_reset ( inner );
				strata_bind_int64 ( inner, 1, id );
				nested = nested && strata_step ( inner ) == STRATA_ROW && strata_column_int64 ( inner, 0 ) == rows;

			}

		}

		expect ( rows == 1002, "stepping visits every row" );
		expect ( nested, "a second statement runs while the first is open" );
		expect ( strata_step ( scan ) == STRATA_ROW && strata_column_int64 ( scan, 0 ) == 1, "stepping after the end starts again" );
		expect ( base::same ( strata_column_text ( scan, 1 ), "AAPL" ) && strata_column_double ( scan, 2 ) == 189.25, "column values read back" );
		expect ( base::same ( strata_column_text ( scan, 0 ), "1" ) && strata_column_bytes ( scan, 1 ) == 4, "numbers read as text" );
		strata_step ( scan );
		expect ( strata_reset ( scan ) == STRATA_OK && strata_step ( scan ) == STRATA_ROW && strata_column_int64 ( scan, 0 ) == 1, "reset in the middle starts over" );
		strata_finalize ( inner );
		strata_finalize ( scan );

	}

	static void fetching ( strata_db* db ) {

		strata_stmt* scan = nullptr;
		strata_value buffer [ 64 * 2 ];
		int total = 0;
		int64_t sum = 0;
		int code = STRATA_ROW;

		strata_prepare ( db, "SELECT size, symbol FROM ticks", -1, &scan, nullptr );

		while ( code == STRATA_ROW ) {

			int rows = 0;

			code = strata_fetch ( scan, buffer, 64, &rows );

			for ( int r = 0; r < rows; r++ )
				sum += buffer [ r * 2 ].data.integer;

			total += rows;

		}

		expect ( code == STRATA_DONE && total == 1002, "fetch reads every row in blocks" );
		expect ( sum == 100 + 500500 + 3001, "fetched values are right" );
		strata_finalize ( scan );

	}

	static void problems ( strata_db* db ) {

		strata_stmt* stmt = nullptr;
		const char* tail = nullptr;
		const char* both = "SELECT 1; SELECT 2";

		expect ( strata_prepare ( db, "SELEC 1", -1, &stmt, nullptr ) == STRATA_ERROR && !stmt, "a syntax error fails to prepare" );
		expect ( strata_error_line ( db ) == 1, "the syntax error has a line" );
		expect ( strata_prepare ( db, both, -1, &stmt, &tail ) == STRATA_OK && stmt && tail && tail > both && tail < both + 18, "prepare reports the tail" );
		strata_finalize ( stmt );
		expect ( strata_prepare ( db, "SELECT 7 AND garbage", 8, &stmt, nullptr ) == STRATA_OK && strata_step ( stmt ) == STRATA_ROW && strata_column_int64 ( stmt, 0 ) == 7, "a sized prepare reads only that many bytes" );
		expect ( strata_exec ( db, "SELECT * FROM nowhere" ) == STRATA_ERROR, "exec reports errors" );

	}

	void api_tests ( ) {

		section ( "C API" );

		char path [ FILE_PATH_ROOM ];
		strata_db* db = nullptr;

		if ( !expect ( scratch_path ( path, sizeof ( path ), "strata-test-api.sdb" ), "a scratch path exists" ) )
			return;

		forget_database ( path );
		expect ( strata_open ( path, &db ) == STRATA_OK, "strata_open opens a new database" );
		expect ( strata_exec ( db, "CREATE TABLE ticks ( id INTEGER PRIMARY KEY, symbol TEXT NOT NULL, price REAL, size INTEGER, note TEXT, UNIQUE ( symbol, size ) )" ) == STRATA_OK, "exec creates a table" );
		bound_inserts ( db );
		batch_inserts ( db );
		stepping ( db );
		fetching ( db );
		problems ( db );
		expect ( strata_close ( db ) == STRATA_OK, "close finalizes what is left" );
		expect ( strata_open ( path, &db ) == STRATA_OK && single ( db, "SELECT count ( * ) FROM ticks" ) == 1002, "the rows survive a reopen" );
		strata_close ( db );
		forget_database ( path );

	}

}
