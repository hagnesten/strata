#include "bench/workloads/workloads.hh"

namespace bench {

	bool insert_items ( bench_driver& driver, const bench_data& data, bench_tally& tally, uint64_t& operations ) {

		operations = data.items;

		if ( !driver.exec ( driver.state, "BEGIN" ) )
			return false;

		bool fine = repeat_statement ( driver, "INSERT INTO items ( a, b, c, d ) VALUES ( ?, ?, ?, ? )", data.items, tally, [ & ] ( uint32_t i, bench_value* values ) {

			values [ 0 ] = integer_of ( data.item_a [ i ] );
			values [ 1 ] = word_of ( data, i );
			values [ 2 ] = real_of ( data.item_c [ i ] );
			values [ 3 ] = integer_of ( data.item_d [ i ] );

			return 4u;

		} );

		return driver.exec ( driver.state, fine ? "COMMIT" : "ROLLBACK" ) && fine;

	}

	bool insert_orders ( bench_driver& driver, const bench_data& data, bench_tally& tally, uint64_t& operations ) {

		operations = data.orders;

		if ( !driver.exec ( driver.state, "BEGIN" ) )
			return false;

		bool fine = repeat_statement ( driver, "INSERT INTO orders ( item, qty, note ) VALUES ( ?, ?, ? )", data.orders, tally, [ & ] ( uint32_t i, bench_value* values ) {

			values [ 0 ] = integer_of ( data.order_item [ i ] );
			values [ 1 ] = integer_of ( data.order_qty [ i ] );
			values [ 2 ] = word_of ( data, ( i * 7 ) % data.items );

			return 3u;

		} );

		return driver.exec ( driver.state, fine ? "COMMIT" : "ROLLBACK" ) && fine;

	}

	bool autocommit_inserts ( bench_driver& driver, const bench_data& data, bench_tally& tally, uint64_t& operations ) {

		operations = BENCH_AUTOCOMMITS;

		return repeat_statement ( driver, "INSERT INTO log ( v ) VALUES ( ? )", BENCH_AUTOCOMMITS, tally, [ & ] ( uint32_t i, bench_value* values ) {

			values [ 0 ] = word_of ( data, i );

			return 1u;

		} );

	}

	bool index_items_a ( bench_driver& driver, const bench_data& data, bench_tally& tally, uint64_t& operations ) {

		operations = data.items;

		return driver.exec ( driver.state, "CREATE INDEX items_a ON items ( a )" );

	}

	bool index_items_b ( bench_driver& driver, const bench_data& data, bench_tally& tally, uint64_t& operations ) {

		operations = data.items;

		return driver.exec ( driver.state, "CREATE INDEX items_b ON items ( b )" );

	}

	bool index_orders_item ( bench_driver& driver, const bench_data& data, bench_tally& tally, uint64_t& operations ) {

		operations = data.orders;

		return driver.exec ( driver.state, "CREATE INDEX orders_item ON orders ( item )" );

	}

}
