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

	bool same_folded ( const char* a, const char* b ) {

		if ( !a || !b )
			return a == b;

		while ( *a && lower ( *a ) == lower ( *b ) ) {

			a++;
			b++;

		}

		return lower ( *a ) == lower ( *b );

	}

	bool starts_with ( const char* value, const char* prefix ) {

		while ( *prefix )
			if ( *value++ != *prefix++ )
				return false;

		return true;

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

	text& text::add ( char value ) {

		if ( reserve ( size + 1 ) ) {

			data [ size++ ] = value;
			data [ size ] = 0;

		}

		return *this;

	}

	text& text::add ( const char* value, size_t count ) {

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

	text& text::real ( double value ) {

		if ( value != value )
			return add ( "NaN" );

		if ( value < 0 ) {

			add ( '-' );
			value = -value;

		}

		if ( value > 1e18 )
			return add ( "1e18+" );

		uint64_t whole = static_cast < uint64_t > ( value );
		double part = value - static_cast < double > ( whole );
		uint64_t scaled = static_cast < uint64_t > ( part * 1000000.0 + 0.5 );

		if ( scaled >= 1000000 ) {

			whole++;
			scaled -= 1000000;

		}

		number ( whole );

		if ( !scaled )
			return add ( ".0" );

		char digits [ 6 ];

		for ( int32_t i = 5; i >= 0; i-- ) {

			digits [ i ] = static_cast < char > ( '0' + scaled % 10 );
			scaled /= 10;

		}

		int32_t last = 5;

		while ( last > 0 && digits [ last ] == '0' )
			last--;

		add ( '.' );

		return add ( digits, static_cast < size_t > ( last + 1 ) );

	}

	text& text::hex ( uint64_t value, uint32_t digits ) {

		static const char table [ ] = "0123456789abcdef";

		for ( int32_t shift = static_cast < int32_t > ( digits - 1 ) * 4; shift >= 0; shift -= 4 )
			add ( table [ value >> shift & 15 ] );

		return *this;

	}

	text& text::repeat ( char value, size_t count ) {

		for ( size_t i = 0; i < count; i++ )
			add ( value );

		return *this;

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
