#include "base/hash/crc32c.hh"

namespace base {

	static uint32_t table [ 8 ] [ 256 ];
	static bool table_ready;

	static void build_table ( ) {

		for ( uint32_t i = 0; i < 256; i++ ) {

			uint32_t value = i;

			for ( uint32_t k = 0; k < 8; k++ )
				value = value & 1 ? value >> 1 ^ 0x82F63B78u : value >> 1;

			table [ 0 ] [ i ] = value;

		}

		for ( uint32_t i = 0; i < 256; i++ )
			for ( uint32_t slice = 1; slice < 8; slice++ )
				table [ slice ] [ i ] = table [ slice - 1 ] [ i ] >> 8 ^ table [ 0 ] [ table [ slice - 1 ] [ i ] & 0xFF ];

		table_ready = true;

	}

	uint32_t crc32c ( const void* data, size_t size, uint32_t start ) {

		if ( !table_ready )
			build_table ( );

		const uint8_t* at = static_cast < const uint8_t* > ( data );
		uint32_t value = ~start;

		while ( size >= 8 ) {

			uint32_t low = value ^ ( static_cast < uint32_t > ( at [ 0 ] ) | static_cast < uint32_t > ( at [ 1 ] ) << 8 | static_cast < uint32_t > ( at [ 2 ] ) << 16 | static_cast < uint32_t > ( at [ 3 ] ) << 24 );
			uint32_t high = static_cast < uint32_t > ( at [ 4 ] ) | static_cast < uint32_t > ( at [ 5 ] ) << 8 | static_cast < uint32_t > ( at [ 6 ] ) << 16 | static_cast < uint32_t > ( at [ 7 ] ) << 24;

			value = table [ 7 ] [ low & 0xFF ] ^ table [ 6 ] [ low >> 8 & 0xFF ] ^ table [ 5 ] [ low >> 16 & 0xFF ] ^ table [ 4 ] [ low >> 24 ] ^ table [ 3 ] [ high & 0xFF ] ^ table [ 2 ] [ high >> 8 & 0xFF ] ^ table [ 1 ] [ high >> 16 & 0xFF ] ^ table [ 0 ] [ high >> 24 ];
			at += 8;
			size -= 8;

		}

		while ( size-- )
			value = value >> 8 ^ table [ 0 ] [ ( value ^ *at++ ) & 0xFF ];

		return ~value;

	}

}
