#pragma once

#include <stdint.h>
#include <stddef.h>

namespace base {

	inline uint16_t read16 ( const uint8_t* at ) {

		return static_cast < uint16_t > ( at [ 0 ] | at [ 1 ] << 8 );

	}

	inline uint32_t read32 ( const uint8_t* at ) {

		return static_cast < uint32_t > ( at [ 0 ] ) | static_cast < uint32_t > ( at [ 1 ] ) << 8 | static_cast < uint32_t > ( at [ 2 ] ) << 16 | static_cast < uint32_t > ( at [ 3 ] ) << 24;

	}

	inline uint64_t read64 ( const uint8_t* at ) {

		return static_cast < uint64_t > ( read32 ( at ) ) | static_cast < uint64_t > ( read32 ( at + 4 ) ) << 32;

	}

	inline void write16 ( uint8_t* at, uint32_t value ) {

		at [ 0 ] = static_cast < uint8_t > ( value );
		at [ 1 ] = static_cast < uint8_t > ( value >> 8 );

	}

	inline void write32 ( uint8_t* at, uint32_t value ) {

		at [ 0 ] = static_cast < uint8_t > ( value );
		at [ 1 ] = static_cast < uint8_t > ( value >> 8 );
		at [ 2 ] = static_cast < uint8_t > ( value >> 16 );
		at [ 3 ] = static_cast < uint8_t > ( value >> 24 );

	}

	inline void write64 ( uint8_t* at, uint64_t value ) {

		write32 ( at, static_cast < uint32_t > ( value ) );
		write32 ( at + 4, static_cast < uint32_t > ( value >> 32 ) );

	}

	inline size_t write_varint ( uint8_t* at, uint64_t value ) {

		size_t size = 0;

		do {

			uint8_t byte = static_cast < uint8_t > ( value & 0x7F );

			value >>= 7;
			at [ size++ ] = static_cast < uint8_t > ( byte | ( value ? 0x80 : 0 ) );

		} while ( value );

		return size;

	}

	inline size_t varint_size ( uint64_t value ) {

		size_t size = 1;

		while ( value >>= 7 )
			size++;

		return size;

	}

	__attribute__ ( ( noinline ) ) inline bool read_long_varint ( const uint8_t* data, size_t size, size_t& at, uint64_t& out ) {

		uint64_t value = 0;

		for ( uint32_t shift = 0; shift < 70; shift += 7 ) {

			if ( at >= size )
				return false;

			uint8_t byte = data [ at++ ];

			if ( shift == 63 && byte > 1 )
				return false;

			value |= static_cast < uint64_t > ( byte & 0x7F ) << shift;

			if ( !( byte & 0x80 ) ) {

				out = value;

				return true;

			}

		}

		return false;

	}

	__attribute__ ( ( always_inline ) ) inline bool read_varint ( const uint8_t* data, size_t size, size_t& at, uint64_t& out ) {

		if ( at < size && data [ at ] < 0x80 ) {

			out = data [ at++ ];

			return true;

		}

		return read_long_varint ( data, size, at, out );

	}

	inline uint64_t zigzag ( int64_t value ) {

		return static_cast < uint64_t > ( value ) << 1 ^ static_cast < uint64_t > ( value >> 63 );

	}

	inline int64_t unzigzag ( uint64_t value ) {

		return static_cast < int64_t > ( value >> 1 ) ^ -static_cast < int64_t > ( value & 1 );

	}

}
