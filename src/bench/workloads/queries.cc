#include "bench/workloads/workloads.hh"

namespace bench {

	bool filtered_scans ( bench_driver& driver, const bench_data& data, bench_tally& tally, uint64_t& operations ) {

		operations = 10;

		return repeat_statement ( driver, "SELECT count ( * ), sum ( c ) FROM items WHERE d = ?", 10, tally, [ & ] ( uint32_t i, bench_value* values ) {

			values [ 0 ] = integer_of ( i * 7 % 100 );

			return 1u;

		} );

	}

	bool like_scans ( bench_driver& driver, const bench_data& data, bench_tally& tally, uint64_t& operations ) {

		static const char* const patterns [ 5 ] = { "%ab%", "%xyz%", "qq%", "%q", "%mn%o%" };

		operations = 5;

		return repeat_statement ( driver, "SELECT count ( * ) FROM items WHERE b LIKE ?", 5, tally, [ & ] ( uint32_t i, bench_value* values ) {

			values [ 0 ] = text_of ( patterns [ i ], static_cast < uint32_t > ( base::length ( patterns [ i ] ) ) );

			return 1u;

		} );

	}

	bool small_groups ( bench_driver& driver, const bench_data& data, bench_tally& tally, uint64_t& operations ) {

		operations = 1;

		return run_once ( driver, "SELECT d, count ( * ), sum ( c ), min ( a ), max ( a ) FROM items GROUP BY d", tally );

	}

	bool large_groups ( bench_driver& driver, const bench_data& data, bench_tally& tally, uint64_t& operations ) {

		operations = 1;

		return run_once ( driver, "SELECT a, count ( * ), sum ( c ) FROM items GROUP BY a", tally );

	}

	bool top_rows ( bench_driver& driver, const bench_data& data, bench_tally& tally, uint64_t& operations ) {

		operations = 1;

		return run_once ( driver, "SELECT id, b, c FROM items ORDER BY c DESC LIMIT 20", tally );

	}

	bool full_sorts ( bench_driver& driver, const bench_data& data, bench_tally& tally, uint64_t& operations ) {

		operations = 1;

		return run_once ( driver, "SELECT id, c FROM items WHERE d < 50 ORDER BY c", tally );

	}

	bool distinct_counts ( bench_driver& driver, const bench_data& data, bench_tally& tally, uint64_t& operations ) {

		operations = 1;

		return run_once ( driver, "SELECT count ( DISTINCT d ), count ( DISTINCT a ) FROM items", tally );

	}

	bool whole_aggregates ( bench_driver& driver, const bench_data& data, bench_tally& tally, uint64_t& operations ) {

		operations = 1;

		return run_once ( driver, "SELECT count ( * ), avg ( c ), min ( b ), max ( b ), sum ( a ) FROM items", tally );

	}

	bool filtered_joins ( bench_driver& driver, const bench_data& data, bench_tally& tally, uint64_t& operations ) {

		operations = 1;

		return run_once ( driver, "SELECT count ( * ), sum ( o.qty * i.c ) FROM orders o JOIN items i ON i.id = o.item WHERE o.qty > 25", tally );

	}

	bool grouped_joins ( bench_driver& driver, const bench_data& data, bench_tally& tally, uint64_t& operations ) {

		operations = 1;

		return run_once ( driver, "SELECT i.d, count ( * ), sum ( o.qty ) FROM orders o JOIN items i ON i.id = o.item GROUP BY i.d", tally );

	}

	bool in_subqueries ( bench_driver& driver, const bench_data& data, bench_tally& tally, uint64_t& operations ) {

		operations = 5;

		return repeat_statement ( driver, "SELECT count ( * ), sum ( c ) FROM items WHERE id IN ( SELECT item FROM orders WHERE qty = ? )", 5, tally, [ & ] ( uint32_t i, bench_value* values ) {

			values [ 0 ] = integer_of ( 1 + i * 9 );

			return 1u;

		} );

	}

	bool exists_subqueries ( bench_driver& driver, const bench_data& data, bench_tally& tally, uint64_t& operations ) {

		operations = 1;

		return run_once ( driver, "SELECT count ( * ) FROM items i WHERE i.id <= 100000 AND EXISTS ( SELECT 1 FROM orders o WHERE o.item = i.id AND o.qty > 40 )", tally );

	}

	bool final_totals ( bench_driver& driver, const bench_data& data, bench_tally& tally, uint64_t& operations ) {

		operations = 1;

		return run_once ( driver, "SELECT count ( * ), sum ( a ), sum ( c ), sum ( d ), count ( DISTINCT b ) FROM items", tally );

	}

}
