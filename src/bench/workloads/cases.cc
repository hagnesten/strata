#include "bench/workloads/workloads.hh"

namespace bench {

	static const bench_case cases [ ] = {

		{ "insert rows in one transaction", false, insert_items },
		{ "insert orders in one transaction", false, insert_orders },
		{ "insert rows, one commit each", false, autocommit_inserts },
		{ "look up rows by rowid", true, rowid_lookups },
		{ "create an integer index", false, index_items_a },
		{ "create a text index", false, index_items_b },
		{ "create an index on orders", false, index_orders_item },
		{ "look up rows by integer index", true, index_lookups },
		{ "count index ranges", true, index_ranges },
		{ "look up rows by text index", true, text_lookups },
		{ "count text prefix ranges", true, text_prefixes },
		{ "scan with a filter", true, filtered_scans },
		{ "scan with LIKE", true, like_scans },
		{ "GROUP BY with 100 groups", true, small_groups },
		{ "GROUP BY with many groups", true, large_groups },
		{ "ORDER BY with LIMIT", true, top_rows },
		{ "sort half the table", true, full_sorts },
		{ "count DISTINCT", true, distinct_counts },
		{ "aggregate the whole table", true, whole_aggregates },
		{ "join with a filter", true, filtered_joins },
		{ "join with GROUP BY", true, grouped_joins },
		{ "IN subquery", true, in_subqueries },
		{ "correlated EXISTS", true, exists_subqueries },
		{ "update rows by rowid", false, rowid_updates },
		{ "update an indexed column", false, indexed_updates },
		{ "update a range in one statement", false, range_update },
		{ "delete rows by rowid", false, rowid_deletes },
		{ "delete a range in one statement", false, range_delete },
		{ "check the final totals", true, final_totals },

	};

	const bench_case* bench_cases ( uint32_t& count ) {

		count = sizeof ( cases ) / sizeof ( cases [ 0 ] );

		return cases;

	}

}
