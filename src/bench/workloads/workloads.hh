#pragma once

#include "bench/harness/bench.hh"

#define BENCH_SCHEMA "CREATE TABLE items ( id INTEGER PRIMARY KEY, a INTEGER, b TEXT, c REAL, d INTEGER ); CREATE TABLE orders ( id INTEGER PRIMARY KEY, item INTEGER, qty INTEGER, note TEXT ); CREATE TABLE log ( id INTEGER PRIMARY KEY, v TEXT )"
#define BENCH_AUTOCOMMITS 200
#define BENCH_RANGES 20000
#define BENCH_UPDATES 100000
#define BENCH_INDEXED_UPDATES 20000
#define BENCH_DELETES 50000

namespace bench {

	bool insert_items ( bench_driver& driver, const bench_data& data, bench_tally& tally, uint64_t& operations );

	bool insert_orders ( bench_driver& driver, const bench_data& data, bench_tally& tally, uint64_t& operations );

	bool autocommit_inserts ( bench_driver& driver, const bench_data& data, bench_tally& tally, uint64_t& operations );

	bool index_items_a ( bench_driver& driver, const bench_data& data, bench_tally& tally, uint64_t& operations );

	bool index_items_b ( bench_driver& driver, const bench_data& data, bench_tally& tally, uint64_t& operations );

	bool index_orders_item ( bench_driver& driver, const bench_data& data, bench_tally& tally, uint64_t& operations );

	bool rowid_lookups ( bench_driver& driver, const bench_data& data, bench_tally& tally, uint64_t& operations );

	bool index_lookups ( bench_driver& driver, const bench_data& data, bench_tally& tally, uint64_t& operations );

	bool index_ranges ( bench_driver& driver, const bench_data& data, bench_tally& tally, uint64_t& operations );

	bool text_lookups ( bench_driver& driver, const bench_data& data, bench_tally& tally, uint64_t& operations );

	bool text_prefixes ( bench_driver& driver, const bench_data& data, bench_tally& tally, uint64_t& operations );

	bool filtered_scans ( bench_driver& driver, const bench_data& data, bench_tally& tally, uint64_t& operations );

	bool like_scans ( bench_driver& driver, const bench_data& data, bench_tally& tally, uint64_t& operations );

	bool small_groups ( bench_driver& driver, const bench_data& data, bench_tally& tally, uint64_t& operations );

	bool large_groups ( bench_driver& driver, const bench_data& data, bench_tally& tally, uint64_t& operations );

	bool top_rows ( bench_driver& driver, const bench_data& data, bench_tally& tally, uint64_t& operations );

	bool full_sorts ( bench_driver& driver, const bench_data& data, bench_tally& tally, uint64_t& operations );

	bool distinct_counts ( bench_driver& driver, const bench_data& data, bench_tally& tally, uint64_t& operations );

	bool whole_aggregates ( bench_driver& driver, const bench_data& data, bench_tally& tally, uint64_t& operations );

	bool filtered_joins ( bench_driver& driver, const bench_data& data, bench_tally& tally, uint64_t& operations );

	bool grouped_joins ( bench_driver& driver, const bench_data& data, bench_tally& tally, uint64_t& operations );

	bool in_subqueries ( bench_driver& driver, const bench_data& data, bench_tally& tally, uint64_t& operations );

	bool exists_subqueries ( bench_driver& driver, const bench_data& data, bench_tally& tally, uint64_t& operations );

	bool rowid_updates ( bench_driver& driver, const bench_data& data, bench_tally& tally, uint64_t& operations );

	bool indexed_updates ( bench_driver& driver, const bench_data& data, bench_tally& tally, uint64_t& operations );

	bool range_update ( bench_driver& driver, const bench_data& data, bench_tally& tally, uint64_t& operations );

	bool rowid_deletes ( bench_driver& driver, const bench_data& data, bench_tally& tally, uint64_t& operations );

	bool range_delete ( bench_driver& driver, const bench_data& data, bench_tally& tally, uint64_t& operations );

	bool final_totals ( bench_driver& driver, const bench_data& data, bench_tally& tally, uint64_t& operations );

}
