#include "storage/record/record.hh"

namespace storage {

	size_t record_size ( const value* values, uint32_t count ) {

		size_t size = base::varint_size ( count );

		for ( uint32_t i = 0; i < count; i++ ) {

			const value& item = values [ i ];

			size++;

			if ( item.kind == VALUE_INTEGER )
				size += base::varint_size ( base::zigzag ( item.integer ) );
			else if ( item.kind == VALUE_REAL )
				size += 8;
			else if ( item.kind == VALUE_TEXT || item.kind == VALUE_BLOB )
				size += base::varint_size ( item.size ) + item.size;

		}

		return size;

	}

	void write_record ( const value* values, uint32_t count, uint8_t* out ) {

		uint8_t* at = out + base::write_varint ( out, count );

		for ( uint32_t i = 0; i < count; i++ ) {

			const value& item = values [ i ];

			*at++ = static_cast < uint8_t > ( item.kind );

			if ( item.kind == VALUE_INTEGER )
				at += base::write_varint ( at, base::zigzag ( item.integer ) );
			else if ( item.kind == VALUE_REAL ) {

				uint64_t bits = 0;

				base::copy ( &bits, &item.real, 8 );
				base::write64 ( at, bits );
				at += 8;

			} else if ( item.kind == VALUE_TEXT || item.kind == VALUE_BLOB ) {

				at += base::write_varint ( at, item.size );

				if ( item.size )
					base::copy ( at, item.bytes, item.size );

				at += item.size;

			}

		}

	}

	void encode_record ( const value* values, uint32_t count, base::text& out ) {

		size_t size = record_size ( values, count );

		if ( !out.reserve ( out.size + size + 1 ) )
			return;

		write_record ( values, count, reinterpret_cast < uint8_t* > ( out.data + out.size ) );
		out.size += size;
		out.data [ out.size ] = 0;

	}

	static bool decode_one ( const uint8_t* data, size_t size, size_t& at, value& out ) {

		if ( at >= size || data [ at ] > VALUE_BLOB )
			return false;

		out = { };
		out.kind = static_cast < value_kind > ( data [ at++ ] );

		if ( out.kind == VALUE_INTEGER ) {

			uint64_t bits = 0;

			if ( !base::read_varint ( data, size, at, bits ) )
				return false;

			out.integer = base::unzigzag ( bits );

		} else if ( out.kind == VALUE_REAL ) {

			if ( size - at < 8 )
				return false;

			uint64_t bits = base::read64 ( data + at );

			base::copy ( &out.real, &bits, 8 );
			at += 8;

		} else if ( out.kind == VALUE_TEXT || out.kind == VALUE_BLOB ) {

			uint64_t length = 0;

			if ( !base::read_varint ( data, size, at, length ) || length > size - at )
				return false;

			out.bytes = data + at;
			out.size = static_cast < size_t > ( length );
			at += out.size;

		}

		return true;

	}

	bool decode_record ( const uint8_t* data, size_t size, value* out, uint32_t room, uint32_t& count ) {

		size_t at = 0;
		uint64_t columns = 0;

		count = 0;

		if ( !base::read_varint ( data, size, at, columns ) || columns > RECORD_COLUMNS )
			return false;

		for ( uint64_t i = 0; i < columns; i++ ) {

			if ( count < room ) {

				if ( !decode_one ( data, size, at, out [ count ] ) )
					return false;

				count++;

				continue;

			}

			value item;

			if ( !decode_one ( data, size, at, item ) )
				return false;

		}

		return at == size;

	}

	static bool skip_one ( const uint8_t* data, size_t size, size_t& at ) {

		if ( at >= size )
			return false;

		uint8_t kind = data [ at++ ];

		if ( kind == VALUE_NULL )
			return true;

		if ( kind == VALUE_INTEGER ) {

			while ( at < size && data [ at ] & 0x80 )
				at++;

			return at++ < size;

		}

		if ( kind == VALUE_REAL ) {

			if ( size - at < 8 )
				return false;

			at += 8;

			return true;

		}

		uint64_t length = 0;

		if ( ( kind != VALUE_TEXT && kind != VALUE_BLOB ) || !base::read_varint ( data, size, at, length ) || length > size - at )
			return false;

		at += static_cast < size_t > ( length );

		return true;

	}

	bool decode_wanted ( const uint8_t* data, size_t size, value* out, uint32_t room, const bool* wanted, uint32_t limit, uint32_t& count ) {

		size_t at = 0;
		uint64_t columns = 0;

		count = 0;

		if ( !base::read_varint ( data, size, at, columns ) || columns > RECORD_COLUMNS )
			return false;

		uint32_t total = columns < room ? static_cast < uint32_t > ( columns ) : room;
		uint32_t stop = total < limit ? total : limit;

		for ( uint32_t i = 0; i < stop; i++ ) {

			if ( wanted [ i ] ) {

				if ( !decode_one ( data, size, at, out [ i ] ) )
					return false;

			} else {

				if ( !skip_one ( data, size, at ) )
					return false;

				out [ i ] = null_value ( );

			}

		}

		for ( uint32_t i = stop; i < total; i++ )
			out [ i ] = null_value ( );

		count = total;

		return true;

	}

	static int32_t compare_bytes ( const value& a, const value& b ) {

		size_t shorter = base::min ( a.size, b.size );

		for ( size_t i = 0; i < shorter; i++ )
			if ( a.bytes [ i ] != b.bytes [ i ] )
				return a.bytes [ i ] < b.bytes [ i ] ? -1 : 1;

		return a.size == b.size ? 0 : a.size < b.size ? -1 : 1;

	}

	static uint32_t rank ( value_kind kind ) {

		return kind == VALUE_NULL ? 0 : kind == VALUE_INTEGER || kind == VALUE_REAL ? 1 : kind == VALUE_TEXT ? 2 : 3;

	}

	int32_t compare_values ( const value& a, const value& b ) {

		uint32_t left = rank ( a.kind );
		uint32_t right = rank ( b.kind );

		if ( left != right )
			return left < right ? -1 : 1;

		if ( left == 0 )
			return 0;

		if ( left == 1 ) {

			if ( a.kind == VALUE_INTEGER && b.kind == VALUE_INTEGER )
				return a.integer == b.integer ? 0 : a.integer < b.integer ? -1 : 1;

			double x = a.kind == VALUE_INTEGER ? static_cast < double > ( a.integer ) : a.real;
			double y = b.kind == VALUE_INTEGER ? static_cast < double > ( b.integer ) : b.real;

			return x == y ? 0 : x < y ? -1 : 1;

		}

		return compare_bytes ( a, b );

	}

	static int32_t compare_fast ( const uint8_t* a, size_t a_size, size_t& left_at, const uint8_t* b, size_t b_size, size_t& right_at, bool& handled ) {

		handled = false;

		if ( left_at >= a_size || right_at >= b_size )
			return 0;

		uint8_t left_kind = a [ left_at ];
		uint8_t right_kind = b [ right_at ];

		if ( left_kind == VALUE_INTEGER && right_kind == VALUE_INTEGER ) {

			size_t left_next = left_at + 1;
			size_t right_next = right_at + 1;
			uint64_t x = 0;
			uint64_t y = 0;

			if ( !base::read_varint ( a, a_size, left_next, x ) || !base::read_varint ( b, b_size, right_next, y ) )
				return 0;

			left_at = left_next;
			right_at = right_next;
			handled = true;

			int64_t p = base::unzigzag ( x );
			int64_t q = base::unzigzag ( y );

			return p == q ? 0 : p < q ? -1 : 1;

		}

		if ( left_kind == right_kind && ( left_kind == VALUE_TEXT || left_kind == VALUE_BLOB ) ) {

			size_t left_next = left_at + 1;
			size_t right_next = right_at + 1;
			uint64_t x = 0;
			uint64_t y = 0;

			if ( !base::read_varint ( a, a_size, left_next, x ) || !base::read_varint ( b, b_size, right_next, y ) || x > a_size - left_next || y > b_size - right_next )
				return 0;

			size_t shorter = static_cast < size_t > ( x < y ? x : y );
			int32_t order = shorter ? __builtin_memcmp ( a + left_next, b + right_next, shorter ) : 0;

			left_at = left_next + static_cast < size_t > ( x );
			right_at = right_next + static_cast < size_t > ( y );
			handled = true;

			if ( order )
				return order < 0 ? -1 : 1;

			return x == y ? 0 : x < y ? -1 : 1;

		}

		return 0;

	}

	int32_t compare_records ( const uint8_t* a, size_t a_size, const uint8_t* b, size_t b_size, uint32_t columns ) {

		size_t left_at = 0;
		size_t right_at = 0;
		uint64_t left_count = 0;
		uint64_t right_count = 0;

		if ( !base::read_varint ( a, a_size, left_at, left_count ) || !base::read_varint ( b, b_size, right_at, right_count ) )
			return 0;

		uint64_t shared = base::min ( left_count, right_count );

		if ( columns && shared > columns )
			shared = columns;

		for ( uint64_t i = 0; i < shared; i++ ) {

			bool handled = false;
			int32_t order = compare_fast ( a, a_size, left_at, b, b_size, right_at, handled );

			if ( handled ) {

				if ( order )
					return order;

				continue;

			}

			value left;
			value right;

			if ( !decode_one ( a, a_size, left_at, left ) || !decode_one ( b, b_size, right_at, right ) )
				return 0;

			order = compare_values ( left, right );

			if ( order )
				return order;

		}

		if ( columns && shared == columns )
			return 0;

		return left_count == right_count ? 0 : left_count < right_count ? -1 : 1;

	}

	bool field_at ( const uint8_t* data, size_t size, uint32_t column, value& out, bool& present ) {

		size_t at = 0;
		uint64_t columns = 0;

		present = false;

		if ( !base::read_varint ( data, size, at, columns ) || columns > RECORD_COLUMNS )
			return false;

		if ( column >= columns )
			return true;

		for ( uint32_t i = 0; i < column; i++ ) {

			if ( at >= size )
				return false;

			uint8_t kind = data [ at++ ];

			if ( kind == VALUE_INTEGER ) {

				while ( at < size && data [ at ] & 0x80 )
					at++;

				if ( at++ >= size )
					return false;

			} else if ( kind == VALUE_REAL ) {

				if ( size - at < 8 )
					return false;

				at += 8;

			} else if ( kind == VALUE_TEXT || kind == VALUE_BLOB ) {

				uint64_t length = 0;

				if ( !base::read_varint ( data, size, at, length ) || length > size - at )
					return false;

				at += static_cast < size_t > ( length );

			} else if ( kind != VALUE_NULL )
				return false;

		}

		present = true;

		return decode_one ( data, size, at, out );

	}

	bool last_integer ( const uint8_t* data, size_t size, int64_t& out ) {

		size_t at = 0;
		uint64_t columns = 0;

		if ( !base::read_varint ( data, size, at, columns ) || !columns || columns > RECORD_COLUMNS )
			return false;

		for ( uint64_t i = 1; i < columns; i++ )
			if ( !skip_one ( data, size, at ) )
				return false;

		uint64_t bits = 0;

		if ( at >= size || data [ at ] != VALUE_INTEGER )
			return false;

		at++;

		if ( !base::read_varint ( data, size, at, bits ) )
			return false;

		out = base::unzigzag ( bits );

		return true;

	}

	void make_probe ( const uint8_t* key, size_t size, uint32_t columns, key_probe& out ) {

		size_t at = 0;

		out.key = key;
		out.size = size;
		out.columns = columns;
		out.fields = 0;
		out.count = 0;
		out.decoded = false;

		if ( !base::read_varint ( key, size, at, out.count ) )
			return;

		uint64_t wanted = columns && columns < out.count ? columns : out.count;

		if ( wanted > PROBE_FIELDS )
			return;

		for ( uint32_t i = 0; i < wanted; i++ )
			if ( !decode_one ( key, size, at, out.values [ i ] ) )
				return;

		out.fields = static_cast < uint32_t > ( wanted );
		out.decoded = true;

	}

	int32_t compare_probe ( const uint8_t* data, size_t size, const key_probe& probe ) {

		if ( !probe.decoded )
			return compare_records ( data, size, probe.key, probe.size, probe.columns );

		size_t at = 0;
		uint64_t count = 0;

		if ( !base::read_varint ( data, size, at, count ) )
			return 0;

		uint64_t shared = count < probe.count ? count : probe.count;

		if ( probe.columns && shared > probe.columns )
			shared = probe.columns;

		for ( uint32_t i = 0; i < shared; i++ ) {

			const value& right = probe.values [ i ];

			if ( at >= size )
				return 0;

			uint8_t kind = data [ at ];

			if ( kind == VALUE_INTEGER && right.kind == VALUE_INTEGER ) {

				uint64_t bits = 0;

				at++;

				if ( !base::read_varint ( data, size, at, bits ) )
					return 0;

				int64_t left = base::unzigzag ( bits );

				if ( left != right.integer )
					return left < right.integer ? -1 : 1;

				continue;

			}

			if ( kind == right.kind && ( kind == VALUE_TEXT || kind == VALUE_BLOB ) ) {

				uint64_t length = 0;

				at++;

				if ( !base::read_varint ( data, size, at, length ) || length > size - at )
					return 0;

				size_t shorter = static_cast < size_t > ( length < right.size ? length : right.size );
				int32_t order = shorter ? __builtin_memcmp ( data + at, right.bytes, shorter ) : 0;

				at += static_cast < size_t > ( length );

				if ( order )
					return order < 0 ? -1 : 1;

				if ( length != right.size )
					return length < right.size ? -1 : 1;

				continue;

			}

			value left;

			if ( !decode_one ( data, size, at, left ) )
				return 0;

			int32_t order = compare_values ( left, right );

			if ( order )
				return order;

		}

		if ( probe.columns && shared == probe.columns )
			return 0;

		return count == probe.count ? 0 : count < probe.count ? -1 : 1;

	}

}
