#include "bench/harness/bench.hh"

namespace bench {

	static uint64_t next_random ( uint64_t& state ) {

		uint64_t value = ( state += 0x9E3779B97F4A7C15ull );

		value = ( value ^ value >> 30 ) * 0xBF58476D1CE4E5B9ull;
		value = ( value ^ value >> 27 ) * 0x94D049BB133111EBull;

		return value ^ value >> 31;

	}

	bool make_data ( bench_data& out, uint32_t items ) {

		uint64_t state = 0x5DEECE66Dull;

		out = { };
		out.items = items;
		out.orders = items / 5;
		out.probe_count = items < 200000 ? items : 200000;
		out.item_a = base::make < int64_t > ( items );
		out.item_b = base::make < uint32_t > ( items );
		out.item_b_size = base::make < uint8_t > ( items );
		out.item_c = base::make < double > ( items );
		out.item_d = base::make < int64_t > ( items );
		out.words = base::make < char > ( static_cast < size_t > ( items ) * 24 + 1 );
		out.order_item = base::make < int64_t > ( out.orders + 1 );
		out.order_qty = base::make < int64_t > ( out.orders + 1 );
		out.probes = base::make < uint32_t > ( out.probe_count + 1 );

		if ( !out.item_a || !out.item_b || !out.item_b_size || !out.item_c || !out.item_d || !out.words || !out.order_item || !out.order_qty || !out.probes ) {

			free_data ( out );

			return false;

		}

		uint32_t groups = items / 10 ? items / 10 : 1;
		uint32_t used = 0;

		for ( uint32_t i = 0; i < items; i++ ) {

			uint32_t size = 8 + static_cast < uint32_t > ( next_random ( state ) % 17 );

			out.item_a [ i ] = static_cast < int64_t > ( next_random ( state ) % groups );
			out.item_b [ i ] = used;
			out.item_b_size [ i ] = static_cast < uint8_t > ( size );
			out.item_c [ i ] = static_cast < double > ( next_random ( state ) % 100000 ) / 100.0;
			out.item_d [ i ] = static_cast < int64_t > ( next_random ( state ) % 100 );

			for ( uint32_t k = 0; k < size; k++ )
				out.words [ used++ ] = static_cast < char > ( 'a' + next_random ( state ) % 26 );

		}

		for ( uint32_t i = 0; i < out.orders; i++ ) {

			out.order_item [ i ] = 1 + static_cast < int64_t > ( next_random ( state ) % items );
			out.order_qty [ i ] = 1 + static_cast < int64_t > ( next_random ( state ) % 50 );

		}

		for ( uint32_t i = 0; i < out.probe_count; i++ )
			out.probes [ i ] = static_cast < uint32_t > ( next_random ( state ) % items );

		return true;

	}

	void free_data ( bench_data& item ) {

		base::release ( item.item_a );
		base::release ( item.item_b );
		base::release ( item.item_b_size );
		base::release ( item.item_c );
		base::release ( item.item_d );
		base::release ( item.words );
		base::release ( item.order_item );
		base::release ( item.order_qty );
		base::release ( item.probes );
		item = { };

	}

}
