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

}
