#include "bench/drivers/drivers.hh"
#include "app/main/main.hh"

namespace bench {

	static bool tally_row ( void* context, const storage::value* values, uint32_t count ) {

		bench_tally& tally = *static_cast < bench_tally* > ( context );
		uint64_t row = 0;

		for ( uint32_t i = 0; i < count; i++ ) {

			const storage::value& item = values [ i ];
			uint64_t hash = 0x5555;

			if ( item.kind == storage::VALUE_INTEGER )
				hash = number_hash ( static_cast < double > ( item.integer ) );
			else if ( item.kind == storage::VALUE_REAL )
				hash = number_hash ( item.real );
			else if ( item.kind == storage::VALUE_TEXT || item.kind == storage::VALUE_BLOB )
				hash = text_hash ( item.bytes, item.size );

			row = row * 0x100000001B3ull ^ hash;

		}

		count_row ( tally, row );

		return true;

	}

	static bool strata_open ( void* state, const char* path ) {

		return engine::open_database ( static_cast < strata_side* > ( state )->db, path );

	}

	static void strata_close ( void* state ) {

		engine::close_database ( static_cast < strata_side* > ( state )->db );

	}

	static bool strata_exec ( void* state, const char* sql ) {

		return engine::execute ( static_cast < strata_side* > ( state )->db, sql, nullptr, 0, nullptr );

	}

	static void* strata_prepare ( void* state, const char* sql ) {

		engine::prepared_statement* made = nullptr;

		if ( !engine::prepare_statement ( static_cast < strata_side* > ( state )->db, sql, made, nullptr ) )
			return nullptr;

		return made;

	}

	static bool strata_run ( void* state, void* statement, const bench_value* values, uint32_t count, bench_tally& tally ) {

		strata_side& side = *static_cast < strata_side* > ( state );

		for ( uint32_t i = 0; i < count; i++ ) {

			const bench_value& item = values [ i ];

			if ( item.kind == BENCH_INTEGER )
				side.bound [ i ] = storage::integer_value ( item.integer );
			else if ( item.kind == BENCH_REAL )
				side.bound [ i ] = storage::real_value ( item.real );
			else if ( item.kind == BENCH_TEXT )
				side.bound [ i ] = storage::text_value ( item.text, item.size );
			else
				side.bound [ i ] = storage::null_value ( );

		}

		engine::query_sink sink = { &tally, nullptr, tally_row };

		return engine::run_prepared ( *static_cast < engine::prepared_statement* > ( statement ), side.bound, count, &sink );

	}

	static void strata_finish ( void* state, void* statement ) {

		engine::finish_statement ( static_cast < engine::prepared_statement* > ( statement ) );

	}

	static const char* strata_problem ( void* state ) {

		return static_cast < strata_side* > ( state )->db.problem.value ( );

	}

	bool make_strata_driver ( bench_driver& out ) {

		strata_side* side = base::make < strata_side > ( 1 );

		out = { };

		if ( !side )
			return false;

		out.name = "Strata";
		out.version = STRATA_VERSION;
		out.state = side;
		out.open = strata_open;
		out.close = strata_close;
		out.exec = strata_exec;
		out.prepare = strata_prepare;
		out.run = strata_run;
		out.finish = strata_finish;
		out.problem = strata_problem;

		return true;

	}

}
