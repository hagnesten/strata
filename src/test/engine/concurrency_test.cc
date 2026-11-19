#include "test/engine/checks.hh"

namespace test {

	static int64_t count_of ( engine::database& db, const char* sql ) {

		result_capture capture;

		if ( !query ( db, sql, capture ) || !capture.rows )
			return -1;

		int64_t value = 0;
		bool negative = capture.out.size && capture.out.data [ 0 ] == '-';

		for ( size_t i = negative ? 1 : 0; i < capture.out.size && capture.out.data [ i ] >= '0' && capture.out.data [ i ] <= '9'; i++ )
			value = value * 10 + ( capture.out.data [ i ] - '0' );

		return negative ? -value : value;

	}

	static bool fails_with ( engine::database& db, const char* sql, const char* part ) {

		result_capture capture;

		return !query ( db, sql, capture ) && contains ( db.problem.value ( ), part );

	}

	static void visibility ( engine::database& a, engine::database& b ) {

		run ( a, "CREATE TABLE shared_rows ( id INTEGER PRIMARY KEY, note TEXT, amount INTEGER )" );
		run ( a, "INSERT INTO shared_rows ( note, amount ) VALUES ( 'one', 1 ), ( 'two', 2 )" );
		expect ( count_of ( b, "SELECT count ( * ) FROM shared_rows" ) == 2, "a second connection sees a new table and its rows" );
		run ( b, "INSERT INTO shared_rows ( note, amount ) VALUES ( 'three', 3 )" );
		expect ( count_of ( a, "SELECT sum ( amount ) FROM shared_rows" ) == 6, "the first connection sees the second one's insert" );

	}

	static void isolation ( engine::database& a, engine::database& b ) {

		run ( b, "BEGIN" );
		expect ( count_of ( b, "SELECT count ( * ) FROM shared_rows" ) == 3, "a reader starts with three rows" );
		run ( a, "INSERT INTO shared_rows ( note, amount ) VALUES ( 'four', 4 )" );
		expect ( count_of ( b, "SELECT count ( * ) FROM shared_rows" ) == 3, "an open read transaction keeps its snapshot" );
		expect ( count_of ( a, "SELECT count ( * ) FROM shared_rows" ) == 4, "the writer sees its own commit" );
		b.pages.wal.busy_milliseconds = 0;
		expect ( fails_with ( b, "INSERT INTO shared_rows ( note, amount ) VALUES ( 'late', 0 )", "changed after" ), "writing from a stale snapshot is refused" );
		run ( b, "ROLLBACK" );
		expect ( count_of ( b, "SELECT count ( * ) FROM shared_rows" ) == 4, "a new statement sees the latest rows" );

	}

	static void writer_lock ( engine::database& a, engine::database& b ) {

		run ( a, "BEGIN IMMEDIATE" );
		run ( a, "INSERT INTO shared_rows ( note, amount ) VALUES ( 'held', 5 )" );
		b.pages.wal.busy_milliseconds = 0;
		expect ( fails_with ( b, "INSERT INTO shared_rows ( note, amount ) VALUES ( 'blocked', 0 )", "busy" ), "a second writer is told the database is busy" );
		expect ( count_of ( b, "SELECT count ( * ) FROM shared_rows" ) == 4, "readers keep working while a writer holds the lock" );
		run ( a, "COMMIT" );
		run ( b, "INSERT INTO shared_rows ( note, amount ) VALUES ( 'after', 6 )" );
		expect ( count_of ( a, "SELECT count ( * ) FROM shared_rows" ) == 6, "the second writer goes ahead once the lock is free" );
		b.pages.wal.busy_milliseconds = SHARED_BUSY_MILLISECONDS;

	}

	static void checkpoints ( engine::database& a, engine::database& b ) {

		run ( a, "CREATE TABLE shared_bulk ( id INTEGER PRIMARY KEY, body TEXT )" );
		run ( a, "INSERT INTO shared_bulk ( body ) VALUES ( 'seed' )" );
		run ( b, "BEGIN" );

		int64_t before = count_of ( b, "SELECT count ( * ) FROM shared_bulk" );
		uint64_t checkpoints = a.pages.stats.checkpoints;

		for ( uint32_t round = 0; round < 30; round++ )
			run ( a, "WITH RECURSIVE n ( i ) AS ( SELECT 1 UNION ALL SELECT i + 1 FROM n WHERE i < 200 ) INSERT INTO shared_bulk ( body ) SELECT printf ( '%0500d', i ) FROM n" );

		expect ( count_of ( b, "SELECT count ( * ) FROM shared_bulk" ) == before, "a long reader keeps its snapshot across checkpoints" );
		expect ( a.pages.wal.snapshot.backfilled <= b.pages.wal.snapshot.frames || a.pages.wal.snapshot.generation == b.pages.wal.snapshot.generation, "a checkpoint never copies past an active reader" );
		run ( b, "COMMIT" );
		expect ( count_of ( b, "SELECT count ( * ) FROM shared_bulk" ) == before + 6000, "the reader sees every row once it starts again" );

		for ( uint32_t round = 0; round < 10; round++ )
			run ( a, "WITH RECURSIVE n ( i ) AS ( SELECT 1 UNION ALL SELECT i + 1 FROM n WHERE i < 200 ) INSERT INTO shared_bulk ( body ) SELECT printf ( '%0500d', i ) FROM n" );

		expect ( a.pages.stats.checkpoints > checkpoints, "the log is folded and reset once readers move on" );
		expect ( count_of ( b, "SELECT count ( * ) FROM shared_bulk" ) == before + 8000, "the reader sees rows after the log was reset" );
		expect ( count_of ( b, "SELECT count ( * ) FROM shared_bulk WHERE length ( body ) = 500" ) == 8000, "every row reads back whole" );

	}

	static void schema_changes ( engine::database& a, engine::database& b ) {

		run ( a, "ALTER TABLE shared_rows ADD COLUMN tag TEXT DEFAULT 'x'" );
		rows ( b, "SELECT note, tag FROM shared_rows WHERE id = 1", "one|x" );
		run ( b, "CREATE INDEX shared_amount ON shared_rows ( amount )" );
		rows ( a, "SELECT note FROM shared_rows WHERE amount = 5", "held" );
		fails ( a, "VACUUM", "other connection" );

	}

	void concurrency_tests ( const char* path ) {

		section ( "connections" );

		engine::database* a = base::make < engine::database > ( 1 );
		engine::database* b = base::make < engine::database > ( 1 );

		forget_database ( path );

		if ( !expect ( a && b && engine::open_database ( *a, path ) && engine::open_database ( *b, path ), "two connections open one file" ) )
			return;

		visibility ( *a, *b );
		isolation ( *a, *b );
		writer_lock ( *a, *b );
		checkpoints ( *a, *b );
		schema_changes ( *a, *b );
		engine::close_database ( *a );
		expect ( count_of ( *b, "SELECT count ( * ) FROM shared_rows" ) == 6, "the other connection keeps working after one closes" );
		expect ( engine::open_database ( *a, path ) && count_of ( *a, "SELECT count ( * ) FROM shared_bulk" ) == 8001, "a reopened connection sees everything" );
		engine::close_database ( *a );
		engine::close_database ( *b );
		expect ( engine::open_database ( *a, path ) && count_of ( *a, "SELECT sum ( amount ) FROM shared_rows" ) == 21, "the data survives every connection closing" );
		engine::close_database ( *a );
		a->~database ( );
		b->~database ( );
		base::release ( a );
		base::release ( b );
		forget_database ( path );

	}

}
