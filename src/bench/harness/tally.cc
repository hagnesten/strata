#include "bench/harness/bench.hh"

namespace bench {

	uint64_t mix_hash ( uint64_t value ) {

		value ^= value >> 33;
		value *= 0xFF51AFD7ED558CCDull;
		value ^= value >> 33;
		value *= 0xC4CEB9FE1A85EC53ull;
		value ^= value >> 33;

		return value;

	}

	uint64_t number_hash ( double value ) {

		double scaled = value * 100.0;
		int64_t rounded = static_cast < int64_t > ( scaled >= 0 ? scaled + 0.5 : scaled - 0.5 );

		return mix_hash ( static_cast < uint64_t > ( rounded ) ^ 0x1234567ull );

	}

	uint64_t text_hash ( const void* bytes, size_t size ) {

		const uint8_t* at = static_cast < const uint8_t* > ( bytes );
		uint64_t value = 0xCBF29CE484222325ull;

		for ( size_t i = 0; i < size; i++ )
			value = ( value ^ at [ i ] ) * 0x100000001B3ull;

		return mix_hash ( value );

	}

	void count_row ( bench_tally& tally, uint64_t row ) {

		tally.rows++;
		tally.hash += mix_hash ( row );

	}

}
