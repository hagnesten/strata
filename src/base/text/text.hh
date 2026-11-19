#pragma once

#include "base/runtime/memory.hh"

namespace base {

	size_t length ( const char* value );

	bool same ( const char* a, const char* b );

	bool same_folded ( const char* a, const char* b );

	bool starts_with ( const char* value, const char* prefix );

	void copy_text ( char* out, size_t room, const char* value );

	void copy_span ( char* out, size_t room, const char* value, size_t size );

	bool read_number ( const char* value, uint64_t& out );

	char lower ( char value );

	char upper ( char value );

	struct decimal_digits {

		uint64_t digits;
		int32_t exponent;
		uint32_t count;
		bool any;

	};

	void add_digit ( decimal_digits& item, uint32_t digit, bool fraction );

	double decimal_value ( const decimal_digits& item, int32_t exponent );

	void significant_digits ( double value, uint32_t count, char* out, int32_t& exponent );

	uint32_t next_code ( const uint8_t* bytes, size_t size, size_t& at );

	size_t count_codes ( const uint8_t* bytes, size_t size );

	size_t skip_codes ( const uint8_t* bytes, size_t size, size_t codes );

	struct text {

		char* data = nullptr;
		size_t size = 0;
		size_t room = 0;

		text ( ) = default;

		text ( const text& ) = delete;

		text& operator = ( const text& ) = delete;

		~text ( );

		text& add ( char value ) {

			if ( size + 1 < room ) {

				data [ size++ ] = value;
				data [ size ] = 0;

				return *this;

			}

			return add_slow ( value );

		}

		text& add ( const char* value );

		text& add ( const char* value, size_t count ) {

			if ( count && size + count < room ) {

				copy ( data + size, value, count );
				size += count;
				data [ size ] = 0;

				return *this;

			}

			return add_slow ( value, count );

		}

		text& add_slow ( char value );

		text& add_slow ( const char* value, size_t count );

		text& number ( uint64_t value );

		text& signed_number ( int64_t value );

		text& decimal ( uint64_t value, uint64_t unit, uint32_t places );

		text& real ( double value );

		text& hex ( uint64_t value, uint32_t digits, bool upper = false );

		text& repeat ( char value, size_t count );

		text& code ( uint32_t value );

		text& pad_to ( size_t column );

		const char* value ( ) const;

		void clear ( );

		bool reserve ( size_t wanted );

	};

}
