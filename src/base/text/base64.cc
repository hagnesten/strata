#include "base/text/base64.hh"

namespace base {

	static const char letters [ ] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

	static int32_t letter_value ( char value ) {

		if ( value >= 'A' && value <= 'Z' )
			return value - 'A';

		if ( value >= 'a' && value <= 'z' )
			return value - 'a' + 26;

		if ( value >= '0' && value <= '9' )
			return value - '0' + 52;

		if ( value == '+' )
			return 62;

		if ( value == '/' )
			return 63;

		return -1;

	}

	void add_base64 ( text& out, const void* data, size_t size ) {

		const uint8_t* at = static_cast < const uint8_t* > ( data );

		for ( size_t i = 0; i < size; i += 3 ) {

			uint32_t left = static_cast < uint32_t > ( size - i );
			uint32_t group = static_cast < uint32_t > ( at [ i ] ) << 16;

			if ( left > 1 )
				group |= static_cast < uint32_t > ( at [ i + 1 ] ) << 8;

			if ( left > 2 )
				group |= at [ i + 2 ];

			out.add ( letters [ group >> 18 & 63 ] );
			out.add ( letters [ group >> 12 & 63 ] );
			out.add ( left > 1 ? letters [ group >> 6 & 63 ] : '=' );
			out.add ( left > 2 ? letters [ group & 63 ] : '=' );

		}

	}

	bool read_base64 ( const char* value, size_t size, uint8_t* out, size_t room, size_t& got ) {

		uint32_t group = 0;
		uint32_t bits = 0;
		bool ended = false;

		got = 0;

		for ( size_t i = 0; i < size; i++ ) {

			if ( value [ i ] == '=' ) {

				ended = true;

				continue;

			}

			int32_t part = letter_value ( value [ i ] );

			if ( part < 0 || ended )
				return false;

			group = group << 6 | static_cast < uint32_t > ( part );
			bits += 6;

			if ( bits >= 8 ) {

				bits -= 8;

				if ( got >= room )
					return false;

				out [ got++ ] = static_cast < uint8_t > ( group >> bits );

			}

		}

		return true;

	}

}
