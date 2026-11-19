#include "bench/workloads/workloads.hh"

namespace bench {

	template < typename filler > static bool in_transaction ( bench_driver& driver, const char* sql, uint32_t times, bench_tally& tally, filler fill ) {

		if ( !driver.exec ( driver.state, "BEGIN" ) )
			return false;

		bool fine = repeat_statement ( driver, sql, times, tally, fill );

		return driver.exec ( driver.state, fine ? "COMMIT" : "ROLLBACK" ) && fine;

	}

	bool rowid_updates ( bench_driver& driver, const bench_data& data, bench_tally& tally, uint64_t& operations ) {

		uint32_t count = data.probe_count < BENCH_UPDATES ? data.probe_count : BENCH_UPDATES;

		operations = count;

		return in_transaction ( driver, "UPDATE items SET c = c + 1 WHERE id = ?", count, tally, [ & ] ( uint32_t i, bench_value* values ) {

			values [ 0 ] = integer_of ( data.probes [ i ] + 1 );

			return 1u;

		} );

	}

	bool indexed_updates ( bench_driver& driver, const bench_data& data, bench_tally& tally, uint64_t& operations ) {

		uint32_t count = data.probe_count < BENCH_INDEXED_UPDATES ? data.probe_count : BENCH_INDEXED_UPDATES;

		operations = count;

		return in_transaction ( driver, "UPDATE items SET a = a + 1 WHERE id = ?", count, tally, [ & ] ( uint32_t i, bench_value* values ) {

			values [ 0 ] = integer_of ( data.probes [ data.probe_count - 1 - i ] + 1 );

			return 1u;

		} );

	}

	bool range_update ( bench_driver& driver, const bench_data& data, bench_tally& tally, uint64_t& operations ) {

		operations = 1;

		return driver.exec ( driver.state, "UPDATE items SET d = d + 1 WHERE d < 30" );

	}

	bool rowid_deletes ( bench_driver& driver, const bench_data& data, bench_tally& tally, uint64_t& operations ) {

		uint32_t count = data.probe_count < BENCH_DELETES ? data.probe_count : BENCH_DELETES;

		operations = count;

		return in_transaction ( driver, "DELETE FROM items WHERE id = ?", count, tally, [ & ] ( uint32_t i, bench_value* values ) {

			values [ 0 ] = integer_of ( data.probes [ i * 3 % data.probe_count ] + 1 );

			return 1u;

		} );

	}

	bool range_delete ( bench_driver& driver, const bench_data& data, bench_tally& tally, uint64_t& operations ) {

		operations = 1;

		return driver.exec ( driver.state, "DELETE FROM items WHERE d >= 90" );

	}

}
