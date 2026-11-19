#include "bench/drivers/drivers.hh"

namespace bench {

	static void note ( sqlite_side& side, const char* what ) {

		base::copy_text ( side.problem, sizeof ( side.problem ), what );

	}

	static void note_db ( sqlite_side& side ) {

		note ( side, side.db ? side.api.errmsg ( side.db ) : "the database is not open" );

	}

	static bool sqlite_exec ( void* state, const char* sql ) {

		sqlite_side& side = *static_cast < sqlite_side* > ( state );
		char* problem = nullptr;

		if ( side.api.exec ( side.db, sql, nullptr, nullptr, &problem ) == SQLITE_OK )
			return true;

		note ( side, problem ? problem : "the statement failed" );

		if ( problem )
			side.api.free ( problem );

		return false;

	}

	static bool sqlite_open ( void* state, const char* path ) {

		sqlite_side& side = *static_cast < sqlite_side* > ( state );

		if ( side.api.open_v2 ( path, &side.db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_NOMUTEX, nullptr ) != SQLITE_OK ) {

			note_db ( side );

			return false;

		}

		return sqlite_exec ( state, "PRAGMA page_size = 4096; PRAGMA journal_mode = WAL; PRAGMA synchronous = FULL; PRAGMA cache_size = 2048; PRAGMA temp_store = MEMORY" );

	}

	static void sqlite_close ( void* state ) {

		sqlite_side& side = *static_cast < sqlite_side* > ( state );

		if ( side.db )
			side.api.close_v2 ( side.db );

		side.db = nullptr;

	}

	static void* sqlite_prepare ( void* state, const char* sql ) {

		sqlite_side& side = *static_cast < sqlite_side* > ( state );
		void* statement = nullptr;

		if ( side.api.prepare_v2 ( side.db, sql, -1, &statement, nullptr ) != SQLITE_OK || !statement ) {

			note_db ( side );

			return nullptr;

		}

		return statement;

	}

	static bool sqlite_run ( void* state, void* statement, const bench_value* values, uint32_t count, bench_tally& tally ) {

		sqlite_side& side = *static_cast < sqlite_side* > ( state );
		const sqlite_api& api = side.api;

		for ( uint32_t i = 0; i < count; i++ ) {

			const bench_value& item = values [ i ];
			int slot = static_cast < int > ( i + 1 );

			if ( item.kind == BENCH_INTEGER )
				api.bind_int64 ( statement, slot, item.integer );
			else if ( item.kind == BENCH_REAL )
				api.bind_double ( statement, slot, item.real );
			else if ( item.kind == BENCH_TEXT )
				api.bind_text ( statement, slot, item.text, static_cast < int > ( item.size ), nullptr );
			else
				api.bind_null ( statement, slot );

		}

		for ( ;; ) {

			int code = api.step ( statement );

			if ( code == SQLITE_DONE )
				break;

			if ( code != SQLITE_ROW ) {

				note_db ( side );
				api.reset ( statement );

				return false;

			}

			int columns = api.column_count ( statement );
			uint64_t row = 0;

			for ( int c = 0; c < columns; c++ ) {

				uint64_t hash = 0x5555;

				switch ( api.column_type ( statement, c ) ) {

					case SQLITE_INTEGER:
						hash = number_hash ( static_cast < double > ( api.column_int64 ( statement, c ) ) );
						break;

					case SQLITE_FLOAT:
						hash = number_hash ( api.column_double ( statement, c ) );
						break;

					case SQLITE_TEXT:
					case SQLITE_BLOB: {

						const unsigned char* bytes = api.column_text ( statement, c );
						int size = api.column_bytes ( statement, c );

						hash = text_hash ( bytes, static_cast < size_t > ( size ) );
						break;

					}

					default:
						break;

				}

				row = row * 0x100000001B3ull ^ hash;

			}

			count_row ( tally, row );

		}

		api.reset ( statement );

		return true;

	}

	static void sqlite_finish ( void* state, void* statement ) {

		static_cast < sqlite_side* > ( state )->api.finalize ( statement );

	}

	static const char* sqlite_problem ( void* state ) {

		return static_cast < sqlite_side* > ( state )->problem;

	}

	bool make_sqlite_driver ( bench_driver& out, const char* path ) {

		sqlite_side* side = base::make < sqlite_side > ( 1 );

		out = { };

		if ( !side )
			return false;

		if ( !load_sqlite ( side->api, path ) ) {

			base::release ( side );

			return false;

		}

		out.name = "SQLite";
		out.version = side->api.libversion ( );
		out.state = side;
		out.open = sqlite_open;
		out.close = sqlite_close;
		out.exec = sqlite_exec;
		out.prepare = sqlite_prepare;
		out.run = sqlite_run;
		out.finish = sqlite_finish;
		out.problem = sqlite_problem;

		return true;

	}

	void free_driver ( bench_driver& item ) {

		if ( !item.state )
			return;

		item.close ( item.state );

		if ( base::same ( item.name, "SQLite" ) )
			unload_sqlite ( static_cast < sqlite_side* > ( item.state )->api );
		else
			static_cast < strata_side* > ( item.state )->~strata_side ( );

		base::release ( item.state );
		item = { };

	}

}
