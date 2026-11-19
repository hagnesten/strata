#include "base/platform/linux/zone.hh"

namespace base {

	static uint32_t big32 ( const uint8_t* at ) {

		return static_cast < uint32_t > ( at [ 0 ] ) << 24 | static_cast < uint32_t > ( at [ 1 ] ) << 16 | static_cast < uint32_t > ( at [ 2 ] ) << 8 | at [ 3 ];

	}

	static int64_t big64 ( const uint8_t* at ) {

		return static_cast < int64_t > ( static_cast < uint64_t > ( big32 ( at ) ) << 32 | big32 ( at + 4 ) );

	}

	int64_t zone_offset_seconds ( const uint8_t* data, size_t size, int64_t now ) {

		if ( size < 44 || data [ 0 ] != 'T' || data [ 1 ] != 'Z' || data [ 2 ] != 'i' || data [ 3 ] != 'f' )
			return 0;

		uint32_t utc_count = big32 ( data + 20 );
		uint32_t standard_count = big32 ( data + 24 );
		uint32_t leap_count = big32 ( data + 28 );
		uint32_t time_count = big32 ( data + 32 );
		uint32_t type_count = big32 ( data + 36 );
		uint32_t char_count = big32 ( data + 40 );
		size_t first_block = static_cast < size_t > ( time_count ) * 5 + type_count * 6 + char_count + leap_count * 8 + standard_count + utc_count;
		bool wide = data [ 4 ] >= '2' && 44 + first_block + 44 <= size;
		const uint8_t* header = data;
		size_t time_size = 4;
		size_t leap_size = 8;

		if ( wide ) {

			header = data + 44 + first_block;

			if ( header [ 0 ] != 'T' || header [ 1 ] != 'Z' )
				return 0;

			utc_count = big32 ( header + 20 );
			standard_count = big32 ( header + 24 );
			leap_count = big32 ( header + 28 );
			time_count = big32 ( header + 32 );
			type_count = big32 ( header + 36 );
			char_count = big32 ( header + 40 );
			time_size = 8;
			leap_size = 12;

		}

		const uint8_t* times = header + 44;
		const uint8_t* indexes = times + static_cast < size_t > ( time_count ) * time_size;
		const uint8_t* types = indexes + time_count;
		size_t needed = static_cast < size_t > ( types - data ) + static_cast < size_t > ( type_count ) * 6;

		( void ) leap_size;

		if ( !type_count || needed > size )
			return 0;

		uint32_t chosen = 0;

		for ( uint32_t t = 0; t < type_count; t++ ) {

			if ( !types [ t * 6 + 4 ] ) {

				chosen = t;

				break;

			}

		}

		for ( uint32_t i = 0; i < time_count; i++ ) {

			int64_t when = time_size == 8 ? big64 ( times + i * 8 ) : static_cast < int32_t > ( big32 ( times + i * 4 ) );

			if ( when > now )
				break;

			if ( indexes [ i ] < type_count )
				chosen = indexes [ i ];

		}

		return static_cast < int32_t > ( big32 ( types + chosen * 6 ) );

	}

}
