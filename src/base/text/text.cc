#include "base/text/text.hh"

namespace base {

	size_t length ( const char* value ) {

		size_t size = 0;

		while ( value && value [ size ] )
			size++;

		return size;

	}

	bool same ( const char* a, const char* b ) {

		if ( !a || !b )
			return a == b;

		while ( *a && *a == *b ) {

			a++;
			b++;

		}

		return *a == *b;

	}

	char lower ( char value ) {

		return value >= 'A' && value <= 'Z' ? static_cast < char > ( value + 32 ) : value;

	}

	char upper ( char value ) {

		return value >= 'a' && value <= 'z' ? static_cast < char > ( value - 32 ) : value;

	}

	void add_digit ( decimal_digits& item, uint32_t digit, bool fraction ) {

		item.any = true;

		if ( !item.count && !digit ) {

			if ( fraction )
				item.exponent--;

			return;

		}

		if ( item.count < 19 ) {

			item.digits = item.digits * 10 + digit;
			item.count++;

			if ( fraction )
				item.exponent--;

		} else if ( !fraction )
			item.exponent++;

	}

	double decimal_value ( const decimal_digits& item, int32_t exponent ) {

		static const double powers [ 23 ] = { 1e0, 1e1, 1e2, 1e3, 1e4, 1e5, 1e6, 1e7, 1e8, 1e9, 1e10, 1e11, 1e12, 1e13, 1e14, 1e15, 1e16, 1e17, 1e18, 1e19, 1e20, 1e21, 1e22 };

		if ( !item.digits )
			return 0;

		int32_t total = item.exponent + exponent;
		double value = static_cast < double > ( item.digits );

		if ( total > 330 )
			return 1.7976931348623157e308 * 10;

		if ( total < -360 )
			return 0;

		while ( total > 22 ) {

			value *= 1e22;
			total -= 22;

		}

		while ( total < -22 ) {

			value /= 1e22;
			total += 22;

		}

		return total < 0 ? value / powers [ -total ] : value * powers [ total ];

	}

	uint32_t next_code ( const uint8_t* bytes, size_t size, size_t& at ) {

		uint8_t first = bytes [ at++ ];

		if ( first < 0x80 )
			return first;

		uint32_t extra = first >= 0xF0 ? 3 : first >= 0xE0 ? 2 : first >= 0xC0 ? 1 : 0;
		uint32_t code = first & ( 0x3F >> extra );

		for ( uint32_t i = 0; i < extra && at < size && ( bytes [ at ] & 0xC0 ) == 0x80; i++ )
			code = code << 6 | ( bytes [ at++ ] & 0x3F );

		return code;

	}

	size_t count_codes ( const uint8_t* bytes, size_t size ) {

		size_t count = 0;

		for ( size_t i = 0; i < size; i++ )
			count += ( bytes [ i ] & 0xC0 ) != 0x80;

		return count;

	}

	size_t skip_codes ( const uint8_t* bytes, size_t size, size_t codes ) {

		size_t at = 0;

		while ( codes && at < size ) {

			next_code ( bytes, size, at );
			codes--;

		}

		return at;

	}

	bool same_folded ( const char* a, const char* b ) {

		if ( !a || !b )
			return a == b;

		while ( *a && lower ( *a ) == lower ( *b ) ) {

			a++;
			b++;

		}

		return lower ( *a ) == lower ( *b );

	}

	bool find_folded ( const char* text, const char* part ) {

		if ( !text || !part )
			return false;

		size_t size = length ( part );

		for ( const char* at = text; *at; at++ ) {

			size_t i = 0;

			while ( i < size && at [ i ] && lower ( at [ i ] ) == lower ( part [ i ] ) )
				i++;

			if ( i == size )
				return true;

		}

		return !size;

	}

	bool same_folded_size ( const char* a, const char* b, size_t size ) {

		for ( size_t i = 0; i < size; i++ )
			if ( lower ( a [ i ] ) != lower ( b [ i ] ) )
				return false;

		return true;

	}

	bool starts_with ( const char* value, const char* prefix ) {

		while ( *prefix )
			if ( *value++ != *prefix++ )
				return false;

		return true;

	}

	bool ends_with_folded ( const char* value, const char* suffix ) {

		size_t have = length ( value );
		size_t want = length ( suffix );

		return have >= want && same_folded_size ( value + have - want, suffix, want );

	}

	void copy_span ( char* out, size_t room, const char* value, size_t size ) {

		if ( !room )
			return;

		size_t take = min ( size, room - 1 );

		copy ( out, value, take );
		out [ take ] = 0;

	}

	void copy_text ( char* out, size_t room, const char* value ) {

		copy_span ( out, room, value, length ( value ) );

	}

	bool read_number ( const char* value, uint64_t& out ) {

		uint64_t result = 0;

		if ( !value || !*value )
			return false;

		for ( ; *value; value++ ) {

			if ( *value < '0' || *value > '9' )
				return false;

			uint64_t digit = static_cast < uint64_t > ( *value - '0' );

			if ( result > ( ~0ull - digit ) / 10 )
				return false;

			result = result * 10 + digit;

		}

		out = result;

		return true;

	}

	text::~text ( ) {

		release ( data );

	}

	bool text::reserve ( size_t wanted ) {

		if ( wanted < room )
			return true;

		size_t grown = max < size_t > ( room * 2, max < size_t > ( wanted + 1, 64 ) );
		char* made = static_cast < char* > ( allocate ( grown ) );

		if ( !made )
			return false;

		if ( data )
			copy ( made, data, size + 1 );

		release ( data );
		data = made;
		room = grown;

		return true;

	}

	text& text::add_slow ( char value ) {

		if ( reserve ( size + 1 ) ) {

			data [ size++ ] = value;
			data [ size ] = 0;

		}

		return *this;

	}

	text& text::add_slow ( const char* value, size_t count ) {

		if ( count && reserve ( size + count ) ) {

			copy ( data + size, value, count );
			size += count;
			data [ size ] = 0;

		}

		return *this;

	}

	text& text::add ( const char* value ) {

		return add ( value, length ( value ) );

	}

	text& text::number ( uint64_t value ) {

		char digits [ 24 ];
		uint32_t count = 0;

		do {

			digits [ count++ ] = static_cast < char > ( '0' + value % 10 );
			value /= 10;

		} while ( value );

		while ( count )
			add ( digits [ --count ] );

		return *this;

	}

	text& text::signed_number ( int64_t value ) {

		if ( value < 0 ) {

			add ( '-' );

			return number ( static_cast < uint64_t > ( 0 ) - static_cast < uint64_t > ( value ) );

		}

		return number ( static_cast < uint64_t > ( value ) );

	}

	text& text::decimal ( uint64_t value, uint64_t unit, uint32_t places ) {

		number ( value / unit );

		if ( !places )
			return *this;

		add ( '.' );

		uint64_t rest = value % unit;

		for ( uint32_t i = 0; i < places; i++ ) {

			rest *= 10;
			add ( static_cast < char > ( '0' + rest / unit ) );
			rest %= unit;

		}

		return *this;

	}

	void significant_digits ( double value, uint32_t count, char* out, int32_t& exponent ) {

		static const double up [ 9 ] = { 1e1, 1e2, 1e4, 1e8, 1e16, 1e32, 1e64, 1e128, 1e256 };
		double scaled = value;

		count = count < 1 ? 1 : count > 17 ? 17 : count;
		exponent = 0;

		if ( scaled >= 10 ) {

			for ( int32_t i = 8; i >= 0; i-- ) {

				if ( scaled >= up [ i ] ) {

					scaled /= up [ i ];
					exponent += 1 << i;

				}

			}

		} else if ( scaled < 1 && scaled > 0 ) {

			for ( int32_t i = 8; i >= 0; i-- ) {

				if ( scaled * up [ i ] < 10 ) {

					scaled *= up [ i ];
					exponent -= 1 << i;

				}

			}

			while ( scaled < 1 ) {

				scaled *= 10;
				exponent--;

			}

		}

		uint64_t unit = 1;

		for ( uint32_t i = 1; i < count; i++ )
			unit *= 10;

		uint64_t digits = static_cast < uint64_t > ( scaled * static_cast < double > ( unit ) + 0.5 );

		if ( digits >= unit * 10 ) {

			digits = ( digits + 5 ) / 10;
			exponent++;

		}

		for ( uint32_t i = count; i-- > 0; ) {

			out [ i ] = static_cast < char > ( '0' + digits % 10 );
			digits /= 10;

		}

	}

	text& text::real ( double value ) {

		if ( value != value )
			return add ( "NaN" );

		if ( value < 0 ) {

			add ( '-' );
			value = -value;

		}

		if ( value > 1.7976931348623157e308 )
			return add ( "Inf" );

		if ( value == 0 )
			return add ( "0.0" );

		int32_t exponent = 0;
		char written [ 16 ];
		int32_t count = 15;

		significant_digits ( value, 15, written, exponent );

		while ( count > 1 && written [ count - 1 ] == '0' )
			count--;

		if ( exponent >= -5 && exponent < 15 ) {

			if ( exponent < 0 ) {

				add ( "0." );
				repeat ( '0', static_cast < size_t > ( -exponent - 1 ) );

				return add ( written, static_cast < size_t > ( count ) );

			}

			for ( int32_t i = 0; i <= exponent; i++ )
				add ( i < count ? written [ i ] : '0' );

			add ( '.' );

			if ( count <= exponent + 1 )
				return add ( '0' );

			return add ( written + exponent + 1, static_cast < size_t > ( count - exponent - 1 ) );

		}

		add ( written [ 0 ] ).add ( '.' );

		if ( count > 1 )
			add ( written + 1, static_cast < size_t > ( count - 1 ) );
		else
			add ( '0' );

		add ( exponent < 0 ? "e-" : "e+" );

		uint32_t magnitude = static_cast < uint32_t > ( exponent < 0 ? -exponent : exponent );

		if ( magnitude < 10 )
			add ( '0' );

		return number ( magnitude );

	}

	text& text::hex ( uint64_t value, uint32_t digits, bool upper ) {

		static const char lower_table [ ] = "0123456789abcdef";
		static const char upper_table [ ] = "0123456789ABCDEF";
		const char* table = upper ? upper_table : lower_table;

		for ( int32_t shift = static_cast < int32_t > ( digits - 1 ) * 4; shift >= 0; shift -= 4 )
			add ( table [ value >> shift & 15 ] );

		return *this;

	}

	text& text::repeat ( char value, size_t count ) {

		for ( size_t i = 0; i < count; i++ )
			add ( value );

		return *this;

	}

	text& text::code ( uint32_t value ) {

		if ( value > 0x10FFFF )
			value = 0xFFFD;

		if ( value < 0x80 )
			return add ( static_cast < char > ( value ) );

		if ( value < 0x800 )
			return add ( static_cast < char > ( 0xC0 | value >> 6 ) ).add ( static_cast < char > ( 0x80 | ( value & 0x3F ) ) );

		if ( value < 0x10000 )
			return add ( static_cast < char > ( 0xE0 | value >> 12 ) ).add ( static_cast < char > ( 0x80 | ( value >> 6 & 0x3F ) ) ).add ( static_cast < char > ( 0x80 | ( value & 0x3F ) ) );

		return add ( static_cast < char > ( 0xF0 | value >> 18 ) ).add ( static_cast < char > ( 0x80 | ( value >> 12 & 0x3F ) ) ).add ( static_cast < char > ( 0x80 | ( value >> 6 & 0x3F ) ) ).add ( static_cast < char > ( 0x80 | ( value & 0x3F ) ) );

	}

	text& text::pad_to ( size_t column ) {

		size_t line = 0;

		for ( size_t i = size; i > 0 && data [ i - 1 ] != '\n'; i-- )
			line++;

		return line < column ? repeat ( ' ', column - line ) : add ( ' ' );

	}

	const char* text::value ( ) const {

		return data ? data : "";

	}

	void text::clear ( ) {

		size = 0;

		if ( data )
			data [ 0 ] = 0;

	}

}
