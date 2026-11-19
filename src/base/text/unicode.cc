#include "base/text/unicode.hh"

namespace base {

	size_t utf8_room ( size_t units ) {

		return units * 3 + 1;

	}

	size_t utf8_from_utf16 ( const uint16_t* units, size_t count, char* out ) {

		uint8_t* at = reinterpret_cast < uint8_t* > ( out );

		for ( size_t i = 0; i < count; i++ ) {

			uint32_t code = units [ i ];

			if ( code < 0x80 ) {

				*at++ = static_cast < uint8_t > ( code );

				continue;

			}

			if ( code >= 0xD800 && code <= 0xDBFF && i + 1 < count && units [ i + 1 ] >= 0xDC00 && units [ i + 1 ] <= 0xDFFF ) {

				code = 0x10000 + ( ( code - 0xD800 ) << 10 ) + ( units [ i + 1 ] - 0xDC00 );
				i++;

			} else if ( code >= 0xD800 && code <= 0xDFFF )
				code = 0xFFFD;

			if ( code < 0x800 ) {

				*at++ = static_cast < uint8_t > ( 0xC0 | code >> 6 );
				*at++ = static_cast < uint8_t > ( 0x80 | ( code & 0x3F ) );

			} else if ( code < 0x10000 ) {

				*at++ = static_cast < uint8_t > ( 0xE0 | code >> 12 );
				*at++ = static_cast < uint8_t > ( 0x80 | ( code >> 6 & 0x3F ) );
				*at++ = static_cast < uint8_t > ( 0x80 | ( code & 0x3F ) );

			} else {

				*at++ = static_cast < uint8_t > ( 0xF0 | code >> 18 );
				*at++ = static_cast < uint8_t > ( 0x80 | ( code >> 12 & 0x3F ) );
				*at++ = static_cast < uint8_t > ( 0x80 | ( code >> 6 & 0x3F ) );
				*at++ = static_cast < uint8_t > ( 0x80 | ( code & 0x3F ) );

			}

		}

		*at = 0;

		return static_cast < size_t > ( at - reinterpret_cast < uint8_t* > ( out ) );

	}

	size_t utf16_from_utf8 ( const uint8_t* bytes, size_t size, uint16_t* out ) {

		uint16_t* at = out;
		size_t i = 0;

		while ( i < size ) {

			while ( i + 8 <= size ) {

				uint64_t word;

				__builtin_memcpy ( &word, bytes + i, 8 );

				if ( word & 0x8080808080808080ull )
					break;

				for ( uint32_t k = 0; k < 8; k++ )
					at [ k ] = bytes [ i + k ];

				at += 8;
				i += 8;

			}

			if ( i >= size )
				break;

			uint32_t lead = bytes [ i ];
			uint32_t code = 0xFFFD;
			size_t extra = 0;

			if ( lead < 0x80 ) {

				code = lead;

			} else if ( lead >= 0xC2 && lead < 0xE0 ) {

				code = lead & 0x1F;
				extra = 1;

			} else if ( lead >= 0xE0 && lead < 0xF0 ) {

				code = lead & 0x0F;
				extra = 2;

			} else if ( lead >= 0xF0 && lead < 0xF5 ) {

				code = lead & 0x07;
				extra = 3;

			}

			bool fine = i + extra < size || !extra;

			for ( size_t k = 1; fine && k <= extra; k++ ) {

				uint8_t next = bytes [ i + k ];

				if ( ( next & 0xC0 ) != 0x80 )
					fine = false;
				else
					code = code << 6 | ( next & 0x3F );

			}

			if ( !fine || ( lead >= 0x80 && !extra ) ) {

				code = 0xFFFD;
				extra = 0;

			}

			i += extra + 1;

			if ( code >= 0x10000 ) {

				code -= 0x10000;
				*at++ = static_cast < uint16_t > ( 0xD800 + ( code >> 10 ) );
				*at++ = static_cast < uint16_t > ( 0xDC00 + ( code & 0x3FF ) );

			} else
				*at++ = static_cast < uint16_t > ( code );

		}

		return static_cast < size_t > ( at - out );

	}

}
