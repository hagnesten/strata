#pragma once

#include "engine/run/run.hh"

#define JOIN_SOURCE_ROOM 64
#define JOIN_TERM_ROOM 128
#define JOIN_SEARCH_LIMIT 7

namespace engine {

	struct join_term {

		uint32_t target;
		int32_t column;
		uint64_t needs;

	};

	struct join_graph {

		const prepared_select* plan;
		join_term terms [ JOIN_TERM_ROOM ];
		uint32_t count;
		double rows [ JOIN_SOURCE_ROOM ];
		uint32_t source_count;

	};

	struct join_step {

		double probe_cost;
		double out_rows;

	};

	struct join_search {

		const join_graph* graph;
		uint32_t order [ JOIN_SOURCE_ROOM ];
		uint32_t best [ JOIN_SOURCE_ROOM ];
		double best_cost;
		bool used [ JOIN_SOURCE_ROOM ];

	};

}