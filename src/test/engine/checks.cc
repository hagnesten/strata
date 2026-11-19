#include "test/engine/checks.hh"

namespace test {

	static bool capture_row ( void* context, const storage::value* values, uint32_t count ) {

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

	static bool capture_columns ( void* context, const char* const* names, uint32_t count ) {

		static_cast < result_capture* > ( context )->columns = count;

		return true;

	}

	bool query ( engine::database& db, const char* sql, result_capture& capture ) {

		engine::query_sink sink = { &capture, capture_columns, capture_row };

		capture.out.clear ( );
		capture.rows = 0;
		capture.columns = 0;

		return engine::execute ( db, sql, nullptr, 0, &sink );

	}

	void rows ( engine::database& db, const char* sql, const char* wanted ) {

		result_capture capture;

		if ( !query ( db, sql, capture ) ) {

			base::text line;

			line.add ( sql ).add ( "\n          error: " ).add ( db.problem.value ( ) );
			expect ( false, line.value ( ) );

			return;

		}

		if ( base::same ( capture.out.value ( ), wanted ) ) {

			expect ( true, sql );

			return;

		}

		base::text line;

		line.add ( sql ).add ( "\n          got:    " ).add ( capture.out.value ( ) ).add ( "\n          wanted: " ).add ( wanted );
		expect ( false, line.value ( ) );

	}

	void run ( engine::database& db, const char* sql ) {

		result_capture capture;

		if ( query ( db, sql, capture ) ) {

			expect ( true, sql );

			return;

		}

		base::text line;

		line.add ( sql ).add ( "\n          error: " ).add ( db.problem.value ( ) );
		expect ( false, line.value ( ) );

	}

	bool contains ( const char* text, const char* part ) {

		size_t size = base::length ( part );

		for ( size_t i = 0; text [ i ]; i++ )
			if ( base::same_bytes ( text + i, part, size ) )
				return true;

		return !size;

	}

	void fails ( engine::database& db, const char* sql, const char* fragment ) {

		result_capture capture;

		if ( query ( db, sql, capture ) ) {

			base::text line;

			line.add ( sql ).add ( "\n          ran, but should fail with: " ).add ( fragment );
			expect ( false, line.value ( ) );

			return;

		}

		if ( contains ( db.problem.value ( ), fragment ) ) {

			expect ( true, sql );

			return;

		}

		base::text line;

		line.add ( sql ).add ( "\n          failed with: " ).add ( db.problem.value ( ) ).add ( "\n          wanted:      " ).add ( fragment );
		expect ( false, line.value ( ) );

	}

	void plan_has ( engine::database& db, const char* sql, const char* fragment ) {

		result_capture capture;

		if ( !query ( db, sql, capture ) ) {

			base::text line;

			line.add ( sql ).add ( "\n          error: " ).add ( db.problem.value ( ) );
			expect ( false, line.value ( ) );

			return;

		}

		base::text line;

		line.add ( sql ).add ( "\n          plan:   " ).add ( capture.out.value ( ) ).add ( "\n          wanted: " ).add ( fragment );
		expect ( contains ( capture.out.value ( ), fragment ), line.value ( ) );

	}

	void plan_lacks ( engine::database& db, const char* sql, const char* fragment ) {

		result_capture capture;

		if ( !query ( db, sql, capture ) ) {

			base::text line;

			line.add ( sql ).add ( "\n          error: " ).add ( db.problem.value ( ) );
			expect ( false, line.value ( ) );

			return;

		}

		base::text line;

		line.add ( sql ).add ( "\n          plan:     " ).add ( capture.out.value ( ) ).add ( "\n          unwanted: " ).add ( fragment );
		expect ( !contains ( capture.out.value ( ), fragment ), line.value ( ) );

	}

}