#pragma once

#include "bench/harness/bench.hh"
#include "bench/drivers/sqlite_api.hh"
#include "engine/database/prepared.hh"

namespace bench {

	struct strata_side {

		engine::database db;
		storage::value bound [ BENCH_PARAMETERS ];

	};

	struct sqlite_side {

		sqlite_api api;
		void* db;
		char problem [ BENCH_PROBLEM_ROOM ];

	};

	bool make_strata_driver ( bench_driver& out );

	bool make_sqlite_driver ( bench_driver& out, const char* path );

	void free_driver ( bench_driver& item );

}
