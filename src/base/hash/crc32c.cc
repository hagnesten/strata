#include "base/hash/crc32c.hh"

namespace base {

	static crc_tables tables;

	static uint32_t matrix_times ( const uint32_t* matrix, uint32_t vector ) {

		uint32_t sum = 0;

		while ( vector ) {

			if ( vector & 1 )
				sum ^= *matrix;

			vector >>= 1;
			matrix++;

		}

		return sum;

	}

	static void matrix_square ( uint32_t* square, const uint32_t* matrix ) {

		for ( uint32_t n = 0; n < 32; n++ )
			square [ n ] = matrix_times ( matrix, matrix [ n ] );

	}

	static void zeros_operator ( uint32_t* even, size_t size ) {

		uint32_t odd [ 32 ];
		uint32_t row = 1;

		odd [ 0 ] = CRC_POLY;

		for ( uint32_t n = 1; n < 32; n++ ) {

			odd [ n ] = row;
			row <<= 1;

		}

		matrix_square ( even, odd );
		matrix_square ( odd, even );

		do {

			matrix_square ( even, odd );
			size >>= 1;

			if ( !size )
				return;

			matrix_square ( odd, even );
			size >>= 1;

		} while ( size );

		for ( uint32_t n = 0; n < 32; n++ )
			even [ n ] = odd [ n ];

	}

	static bool cpu_has_crc ( ) {

		uint32_t a = 1;
		uint32_t b = 0;
		uint32_t c = 0;
		uint32_t d = 0;

		__asm__ ( "cpuid" : "+a" ( a ), "=b" ( b ), "+c" ( c ), "=d" ( d ) );

		return c >> 20 & 1;

	}

	static void build_tables ( ) {

		for ( uint32_t i = 0; i < 256; i++ ) {

			uint32_t value = i;

			for ( uint32_t k = 0; k < 8; k++ )
				value = value & 1 ? value >> 1 ^ CRC_POLY : value >> 1;

			tables.slices [ 0 ] [ i ] = value;

		}

		for ( uint32_t i = 0; i < 256; i++ )
			for ( uint32_t slice = 1; slice < 8; slice++ )
				tables.slices [ slice ] [ i ] = tables.slices [ slice - 1 ] [ i ] >> 8 ^ tables.slices [ 0 ] [ tables.slices [ slice - 1 ] [ i ] & 0xFF ];

		uint32_t shift [ 32 ];

		zeros_operator ( shift, CRC_LANE );

		for ( uint32_t n = 0; n < 256; n++ ) {

			tables.lane [ 0 ] [ n ] = matrix_times ( shift, n );
			tables.lane [ 1 ] [ n ] = matrix_times ( shift, n << 8 );
			tables.lane [ 2 ] [ n ] = matrix_times ( shift, n << 16 );
			tables.lane [ 3 ] [ n ] = matrix_times ( shift, n << 24 );

		}

		tables.hardware = cpu_has_crc ( );
		tables.ready = true;

	}

	uint32_t crc32c_portable ( const void* data, size_t size, uint32_t start ) {

		if ( !tables.ready )
			build_tables ( );

		const uint8_t* at = static_cast < const uint8_t* > ( data );
		uint32_t value = ~start;

		while ( size >= 8 ) {

			uint32_t low = value ^ ( static_cast < uint32_t > ( at [ 0 ] ) | static_cast < uint32_t > ( at [ 1 ] ) << 8 | static_cast < uint32_t > ( at [ 2 ] ) << 16 | static_cast < uint32_t > ( at [ 3 ] ) << 24 );
			uint32_t high = static_cast < uint32_t > ( at [ 4 ] ) | static_cast < uint32_t > ( at [ 5 ] ) << 8 | static_cast < uint32_t > ( at [ 6 ] ) << 16 | static_cast < uint32_t > ( at [ 7 ] ) << 24;

			value = tables.slices [ 7 ] [ low & 0xFF ] ^ tables.slices [ 6 ] [ low >> 8 & 0xFF ] ^ tables.slices [ 5 ] [ low >> 16 & 0xFF ] ^ tables.slices [ 4 ] [ low >> 24 ] ^ tables.slices [ 3 ] [ high & 0xFF ] ^ tables.slices [ 2 ] [ high >> 8 & 0xFF ] ^ tables.slices [ 1 ] [ high >> 16 & 0xFF ] ^ tables.slices [ 0 ] [ high >> 24 ];
			at += 8;
			size -= 8;

		}

		while ( size-- )
			value = value >> 8 ^ tables.slices [ 0 ] [ ( value ^ *at++ ) & 0xFF ];

		return ~value;

	}

	static uint32_t shift_lane ( uint32_t value ) {

		return tables.lane [ 0 ] [ value & 0xFF ] ^ tables.lane [ 1 ] [ value >> 8 & 0xFF ] ^ tables.lane [ 2 ] [ value >> 16 & 0xFF ] ^ tables.lane [ 3 ] [ value >> 24 ];

	}

	static uint64_t load64 ( const uint8_t* at ) {

		uint64_t value;

		__builtin_memcpy ( &value, at, 8 );

		return value;

	}

	__attribute__ ( ( target ( "sse4.2" ) ) ) static uint32_t crc_hardware ( const uint8_t* at, size_t size, uint32_t start ) {

		uint64_t value = ~start;

		while ( size >= CRC_LANE * 3 ) {

			uint64_t second = 0;
			uint64_t third = 0;

			for ( uint32_t i = 0; i < CRC_LANE; i += 8 ) {

				value = __builtin_ia32_crc32di ( value, load64 ( at + i ) );
				second = __builtin_ia32_crc32di ( second, load64 ( at + CRC_LANE + i ) );
				third = __builtin_ia32_crc32di ( third, load64 ( at + CRC_LANE * 2 + i ) );

			}

			value = shift_lane ( static_cast < uint32_t > ( value ) ) ^ static_cast < uint32_t > ( second );
			value = shift_lane ( static_cast < uint32_t > ( value ) ) ^ static_cast < uint32_t > ( third );
			at += CRC_LANE * 3;
			size -= CRC_LANE * 3;

		}

		while ( size >= 8 ) {

			value = __builtin_ia32_crc32di ( value, load64 ( at ) );
			at += 8;
			size -= 8;

		}

		uint32_t small = static_cast < uint32_t > ( value );

		while ( size-- )
			small = __builtin_ia32_crc32qi ( small, *at++ );

		return ~small;

	}

	uint32_t crc32c ( const void* data, size_t size, uint32_t start ) {

		if ( !tables.ready )
			build_tables ( );

		if ( tables.hardware )
			return crc_hardware ( static_cast < const uint8_t* > ( data ), size, start );

		return crc32c_portable ( data, size, start );

	}

	bool crc32c_hardware ( ) {

		if ( !tables.ready )
			build_tables ( );

		return tables.hardware;

	}

}
