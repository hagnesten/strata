#include "bench/workloads/workloads.hh"

namespace bench {

	bool rowid_lookups ( bench_driver& driver, const bench_data& data, bench_tally& tally, uint64_t& operations ) {

		operations = data.probe_count;

		return repeat_statement ( driver, "SELECT a, b, c, d FROM items WHERE id = ?", data.probe_count, tally, [ & ] ( uint32_t i, bench_value* values ) {

			values [ 0 ] = integer_of ( data.probes [ i ] + 1 );

			return 1u;

		} );

	}

	bool index_lookups ( bench_driver& driver, const bench_data& data, bench_tally& tally, uint64_t& operations ) {

		operations = data.probe_count;

		return repeat_statement ( driver, "SELECT id, c FROM items WHERE a = ?", data.probe_count, tally, [ & ] ( uint32_t i, bench_value* values ) {

			values [ 0 ] = integer_of ( data.item_a [ data.probes [ i ] ] );

			return 1u;

		} );

	}

	bool index_ranges ( bench_driver& driver, const bench_data& data, bench_tally& tally, uint64_t& operations ) {

		uint32_t count = data.probe_count < BENCH_RANGES ? data.probe_count : BENCH_RANGES;

		operations = count;

		return repeat_statement ( driver, "SELECT count ( * ), sum ( c ) FROM items WHERE a BETWEEN ?1 AND ?2", count, tally, [ & ] ( uint32_t i, bench_value* values ) {

			int64_t low = data.item_a [ data.probes [ i ] ];

			values [ 0 ] = integer_of ( low );
			values [ 1 ] = integer_of ( low + 9 );

			return 2u;

		} );

	}

	bool text_lookups ( bench_driver& driver, const bench_data& data, bench_tally& tally, uint64_t& operations ) {

		operations = data.probe_count;

		return repeat_statement ( driver, "SELECT id FROM items WHERE b = ?", data.probe_count, tally, [ & ] ( uint32_t i, bench_value* values ) {

			values [ 0 ] = word_of ( data, data.probes [ i ] );

			return 1u;

		} );

	}

	bool text_prefixes ( bench_driver& driver, const bench_data& data, bench_tally& tally, uint64_t& operations ) {

		uint32_t count = data.probe_count < BENCH_RANGES ? data.probe_count : BENCH_RANGES;
		char low [ 4 ] = { };
		char high [ 4 ] = { };

		operations = count;

		return repeat_statement ( driver, "SELECT count ( * ) FROM items WHERE b >= ?1 AND b < ?2", count, tally, [ & ] ( uint32_t i, bench_value* values ) {

			const char* word = data.words + data.item_b [ data.probes [ i ] ];

			low [ 0 ] = word [ 0 ];
			low [ 1 ] = word [ 1 ];
			low [ 2 ] = word [ 2 ];
			high [ 0 ] = word [ 0 ];
			high [ 1 ] = word [ 1 ];
			high [ 2 ] = static_cast < char > ( word [ 2 ] + 1 );
			values [ 0 ] = text_of ( low, 3 );
			values [ 1 ] = text_of ( high, 3 );

			return 2u;

		} );

	}

}
