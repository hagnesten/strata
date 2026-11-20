#include "engine/run/run.hh"
#include "base/clock/clock.hh"

#define DATE_DAY 86400000ll
#define DATE_UNIX_EPOCH 210866760000000ll

namespace engine {

	static int64_t julian_of ( int64_t year, int64_t month, int64_t day ) {

		if ( month <= 2 ) {

			year--;
			month += 12;

		}

		int64_t a = year / 100;
		int64_t b = 2 - a + a / 4;
		int64_t x1 = 36525 * ( year + 4716 ) / 100;
		int64_t x2 = 306001 * ( month + 1 ) / 10000;

		return ( x1 + x2 + day + b ) * DATE_DAY - 1524 * DATE_DAY - DATE_DAY / 2;

	}

	static void civil_of ( int64_t moment, int64_t& year, int64_t& month, int64_t& day ) {

		int64_t z = ( moment + DATE_DAY / 2 ) / DATE_DAY;
		int64_t alpha = static_cast < int64_t > ( ( static_cast < double > ( z ) - 1867216.25 ) / 36524.25 );
		int64_t a = z + 1 + alpha - alpha / 4;
		int64_t b = a + 1524;
		int64_t c = static_cast < int64_t > ( ( static_cast < double > ( b ) - 122.1 ) / 365.25 );
		int64_t d = ( 36525 * ( c & 32767 ) ) / 100;
		int64_t e = static_cast < int64_t > ( static_cast < double > ( b - d ) / 30.6001 );
		int64_t x1 = static_cast < int64_t > ( 30.6001 * static_cast < double > ( e ) );

		day = b - d - x1;
		month = e < 14 ? e - 1 : e - 13;
		year = month > 2 ? c - 4716 : c - 4715;

	}

	static int64_t clock_of ( int64_t moment ) {

		return ( moment + DATE_DAY / 2 ) % DATE_DAY;

	}

	static bool digits ( const uint8_t* text, size_t size, size_t& at, uint32_t count, int64_t& out ) {

		out = 0;

		for ( uint32_t i = 0; i < count; i++ ) {

			if ( at >= size || text [ at ] < '0' || text [ at ] > '9' )
				return false;

			out = out * 10 + ( text [ at++ ] - '0' );

		}

		return true;

	}

	static bool read_clock ( const uint8_t* text, size_t size, size_t& at, int64_t& out ) {

		int64_t hour = 0;
		int64_t minute = 0;
		int64_t second = 0;
		int64_t fraction = 0;

		if ( !digits ( text, size, at, 2, hour ) || at >= size || text [ at++ ] != ':' || !digits ( text, size, at, 2, minute ) )
			return false;

		if ( at < size && text [ at ] == ':' ) {

			at++;

			if ( !digits ( text, size, at, 2, second ) )
				return false;

			if ( at < size && text [ at ] == '.' ) {

				int64_t scale = 100;

				at++;

				while ( at < size && text [ at ] >= '0' && text [ at ] <= '9' ) {

					fraction += ( text [ at++ ] - '0' ) * scale;
					scale /= 10;

				}

			}

		}

		if ( hour > 24 || minute > 59 || second > 59 )
			return false;

		out = hour * 3600000 + minute * 60000 + second * 1000 + fraction;

		while ( at < size && text [ at ] == ' ' )
			at++;

		if ( at < size && ( text [ at ] == 'Z' || text [ at ] == 'z' ) )
			at++;
		else if ( at < size && ( text [ at ] == '+' || text [ at ] == '-' ) ) {

			bool behind = text [ at++ ] == '-';
			int64_t zone_hour = 0;
			int64_t zone_minute = 0;

			if ( !digits ( text, size, at, 2, zone_hour ) )
				return false;

			if ( at < size && text [ at ] == ':' )
				at++;

			digits ( text, size, at, 2, zone_minute );

			int64_t shift = zone_hour * 3600000 + zone_minute * 60000;

			out += behind ? shift : -shift;

		}

		return true;

	}

	static bool read_moment ( run_context& ctx, const storage::value& item, bool unix, int64_t& out ) {

		if ( item.kind == storage::VALUE_INTEGER || item.kind == storage::VALUE_REAL ) {

			double number = item.kind == storage::VALUE_INTEGER ? static_cast < double > ( item.integer ) : item.real;

			out = unix ? static_cast < int64_t > ( number * 1000 ) + DATE_UNIX_EPOCH : static_cast < int64_t > ( number * DATE_DAY + 0.5 );

			return true;

		}

		if ( item.kind != storage::VALUE_TEXT )
			return false;

		storage::value text = item;
		const uint8_t* bytes = text.bytes;
		size_t size = text.size;
		size_t at = 0;

		while ( at < size && bytes [ at ] == ' ' )
			at++;

		while ( size > at && bytes [ size - 1 ] == ' ' )
			size--;

		if ( size - at == 3 && base::lower ( static_cast < char > ( bytes [ at ] ) ) == 'n' && base::lower ( static_cast < char > ( bytes [ at + 1 ] ) ) == 'o' && base::lower ( static_cast < char > ( bytes [ at + 2 ] ) ) == 'w' ) {

			out = base::unix_milliseconds ( ) + DATE_UNIX_EPOCH;

			return true;

		}

		storage::value number;

		if ( read_number_text ( bytes + at, size - at, number, true ) && ( size - at < 5 || bytes [ at + 4 ] != '-' ) )
			return read_moment ( ctx, number, unix, out );

		int64_t year = 0;
		int64_t month = 0;
		int64_t day = 0;
		size_t start = at;

		if ( digits ( bytes, size, at, 4, year ) && at < size && bytes [ at ] == '-' ) {

			at++;

			if ( !digits ( bytes, size, at, 2, month ) || at >= size || bytes [ at++ ] != '-' || !digits ( bytes, size, at, 2, day ) || month < 1 || month > 12 || day < 1 || day > 31 )
				return false;

			out = julian_of ( year, month, day );

			if ( at < size && ( bytes [ at ] == ' ' || bytes [ at ] == 'T' || bytes [ at ] == 't' ) ) {

				at++;

				int64_t clock = 0;

				if ( !read_clock ( bytes, size, at, clock ) )
					return false;

				out += clock;

			}

			return at == size;

		}

		at = start;

		int64_t clock = 0;

		if ( !read_clock ( bytes, size, at, clock ) || at != size )
			return false;

		out = julian_of ( 2000, 1, 1 ) + clock;

		return true;

	}

	static bool word_is ( const char* text, size_t size, const char* word ) {

		size_t length = base::length ( word );

		if ( size != length )
			return false;

		for ( size_t i = 0; i < size; i++ )
			if ( base::lower ( text [ i ] ) != word [ i ] )
				return false;

		return true;

	}

	static bool apply_modifier ( run_context& ctx, const storage::value& item, int64_t& moment ) {

		if ( item.kind != storage::VALUE_TEXT )
			return false;

		const char* text = reinterpret_cast < const char* > ( item.bytes );
		size_t size = item.size;

		while ( size && text [ 0 ] == ' ' ) {

			text++;
			size--;

		}

		while ( size && text [ size - 1 ] == ' ' )
			size--;

		if ( word_is ( text, size, "start of day" ) ) {

			moment -= clock_of ( moment );

			return true;

		}

		if ( word_is ( text, size, "start of month" ) || word_is ( text, size, "start of year" ) ) {

			int64_t year = 0;
			int64_t month = 0;
			int64_t day = 0;

			civil_of ( moment, year, month, day );
			moment = julian_of ( year, size == 14 ? month : 1, 1 );

			return true;

		}

		if ( word_is ( text, size, "localtime" ) ) {

			moment += base::local_offset_milliseconds ( );

			return true;

		}

		if ( word_is ( text, size, "utc" ) ) {

			moment -= base::local_offset_milliseconds ( );

			return true;

		}

		if ( size > 8 && word_is ( text, 8, "weekday " ) ) {

			storage::value number;
			int64_t wanted = 0;

			if ( !read_number_text ( reinterpret_cast < const uint8_t* > ( text + 8 ), size - 8, number, true ) || !integer_of ( number, wanted ) || wanted < 0 || wanted > 6 )
				return false;

			int64_t today = ( ( moment + DATE_DAY * 3 / 2 ) / DATE_DAY ) % 7;
			int64_t ahead = ( wanted - today + 7 ) % 7;

			moment += ahead * DATE_DAY;

			return true;

		}

		size_t at = 0;

		while ( at < size && text [ at ] != ' ' )
			at++;

		storage::value number;

		if ( !at || !read_number_text ( reinterpret_cast < const uint8_t* > ( text ), at, number, true ) )
			return false;

		const char* unit = text + at + 1;
		size_t unit_size = at < size ? size - at - 1 : 0;

		if ( unit_size && base::lower ( unit [ unit_size - 1 ] ) == 's' )
			unit_size--;

		double amount = 0;

		real_of ( number, amount );

		if ( word_is ( unit, unit_size, "day" ) )
			moment += static_cast < int64_t > ( amount * DATE_DAY );
		else if ( word_is ( unit, unit_size, "hour" ) )
			moment += static_cast < int64_t > ( amount * 3600000 );
		else if ( word_is ( unit, unit_size, "minute" ) )
			moment += static_cast < int64_t > ( amount * 60000 );
		else if ( word_is ( unit, unit_size, "second" ) )
			moment += static_cast < int64_t > ( amount * 1000 );
		else if ( word_is ( unit, unit_size, "month" ) || word_is ( unit, unit_size, "year" ) ) {

			int64_t year = 0;
			int64_t month = 0;
			int64_t day = 0;
			int64_t whole = static_cast < int64_t > ( amount );
			int64_t clock = clock_of ( moment );

			civil_of ( moment, year, month, day );

			if ( unit_size == 4 )
				year += whole;
			else {

				month += whole;
				year += ( month - 1 ) >= 0 ? ( month - 1 ) / 12 : ( month - 12 ) / 12;
				month = ( ( month - 1 ) % 12 + 12 ) % 12 + 1;

			}

			moment = julian_of ( year, month, 1 ) + ( day - 1 ) * DATE_DAY + clock;

		} else
			return false;

		return true;

	}

	static void two ( base::text& out, int64_t value ) {

		if ( value < 10 )
			out.add ( '0' );

		out.number ( static_cast < uint64_t > ( value ) );

	}

	static void add_date ( base::text& out, int64_t moment ) {

		int64_t year = 0;
		int64_t month = 0;
		int64_t day = 0;

		civil_of ( moment, year, month, day );

		if ( year < 0 ) {

			out.add ( '-' );
			year = -year;

		}

		if ( year < 1000 )
			out.add ( year < 10 ? "000" : year < 100 ? "00" : "0" );

		out.number ( static_cast < uint64_t > ( year ) ).add ( '-' );
		two ( out, month );
		out.add ( '-' );
		two ( out, day );

	}

	static void add_time ( base::text& out, int64_t moment ) {

		int64_t clock = clock_of ( moment );

		two ( out, clock / 3600000 );
		out.add ( ':' );
		two ( out, clock / 60000 % 60 );
		out.add ( ':' );
		two ( out, clock / 1000 % 60 );

	}

	static bool format_moment ( run_context& ctx, const storage::value& pattern, int64_t moment, base::text& out ) {

		storage::value text = text_of ( *ctx.scratch, pattern );
		int64_t year = 0;
		int64_t month = 0;
		int64_t day = 0;

		civil_of ( moment, year, month, day );

		int64_t clock = clock_of ( moment );
		int64_t day_of_year = ( moment - julian_of ( year, 1, 1 ) ) / DATE_DAY;
		int64_t monday = ( ( moment + DATE_DAY / 2 ) / DATE_DAY ) % 7;
		int64_t sunday = ( ( moment + DATE_DAY * 3 / 2 ) / DATE_DAY ) % 7;

		for ( size_t i = 0; i < text.size; i++ ) {

			char here = static_cast < char > ( text.bytes [ i ] );

			if ( here != '%' || i + 1 >= text.size ) {

				out.add ( here );

				continue;

			}

			switch ( text.bytes [ ++i ] ) {

				case 'd': two ( out, day ); break;
				case 'e': if ( day < 10 ) out.add ( ' ' ); out.number ( static_cast < uint64_t > ( day ) ); break;
				case 'f': {

					int64_t milli = clock % 1000;

					two ( out, clock / 1000 % 60 );
					out.add ( '.' ).add ( static_cast < char > ( '0' + milli / 100 ) ).add ( static_cast < char > ( '0' + milli / 10 % 10 ) ).add ( static_cast < char > ( '0' + milli % 10 ) );
					break;

				}
				case 'F': add_date ( out, moment ); break;
				case 'H': two ( out, clock / 3600000 ); break;
				case 'j': { int64_t n = day_of_year + 1; out.add ( n < 10 ? "00" : n < 100 ? "0" : "" ).number ( static_cast < uint64_t > ( n ) ); break; }
				case 'J': out.real ( static_cast < double > ( moment ) / DATE_DAY ); break;
				case 'm': two ( out, month ); break;
				case 'M': two ( out, clock / 60000 % 60 ); break;
				case 's': out.signed_number ( ( moment - DATE_UNIX_EPOCH ) / 1000 ); break;
				case 'S': two ( out, clock / 1000 % 60 ); break;
				case 'T': add_time ( out, moment ); break;
				case 'u': out.number ( static_cast < uint64_t > ( monday + 1 ) ); break;
				case 'w': out.number ( static_cast < uint64_t > ( sunday ) ); break;
				case 'W': two ( out, ( day_of_year + 7 - monday ) / 7 ); break;
				case 'Y': out.signed_number ( year ); break;
				case '%': out.add ( '%' ); break;
				default: return false;

			}

		}

		return true;

	}

	bool call_date_function ( run_context& ctx, function_kind kind, const storage::value* args, uint32_t count, storage::value& out ) {

		out = storage::null_value ( );

		uint32_t first = kind == FUNCTION_STRFTIME ? 1 : 0;
		storage::value now = storage::text_value ( "now", 3 );
		const storage::value& source = count > first ? args [ first ] : now;

		if ( source.kind == storage::VALUE_NULL )
			return true;

		bool unix = false;

		for ( uint32_t i = first + 1; i < count; i++ )
			if ( args [ i ].kind == storage::VALUE_TEXT && word_is ( reinterpret_cast < const char* > ( args [ i ].bytes ), args [ i ].size, "unixepoch" ) )
				unix = true;

		int64_t moment = 0;

		if ( !read_moment ( ctx, source, unix, moment ) )
			return true;

		for ( uint32_t i = first + 1; i < count; i++ ) {

			if ( args [ i ].kind == storage::VALUE_TEXT && ( word_is ( reinterpret_cast < const char* > ( args [ i ].bytes ), args [ i ].size, "unixepoch" ) || word_is ( reinterpret_cast < const char* > ( args [ i ].bytes ), args [ i ].size, "auto" ) ) )
				continue;

			if ( !apply_modifier ( ctx, args [ i ], moment ) )
				return true;

		}

		base::text made;

		switch ( kind ) {

			case FUNCTION_DATE:
				add_date ( made, moment );
				break;

			case FUNCTION_TIME:
				add_time ( made, moment );
				break;

			case FUNCTION_DATETIME:
				add_date ( made, moment );
				made.add ( ' ' );
				add_time ( made, moment );
				break;

			case FUNCTION_NOW: {

				int64_t milli = ( moment % 1000 + 1000 ) % 1000;

				add_date ( made, moment );
				made.add ( ' ' );
				add_time ( made, moment );

				if ( milli ) {

					char digits [ 3 ] = { static_cast < char > ( '0' + milli / 100 ), static_cast < char > ( '0' + milli / 10 % 10 ), static_cast < char > ( '0' + milli % 10 ) };
					size_t used = 3;

					while ( used > 1 && digits [ used - 1 ] == '0' )
						used--;

					made.add ( '.' ).add ( digits, used );

				}

				break;

			}

			case FUNCTION_JULIANDAY:
				out = storage::real_value ( static_cast < double > ( moment ) / DATE_DAY );

				return true;

			case FUNCTION_UNIXEPOCH:
				out = storage::integer_value ( ( moment - DATE_UNIX_EPOCH ) / 1000 );

				return true;

			default:
				if ( args [ 0 ].kind == storage::VALUE_NULL || !format_moment ( ctx, args [ 0 ], moment, made ) )
					return true;

				break;

		}

		char* kept = ctx.scratch->keep ( made.value ( ), made.size );

		if ( !kept )
			return fail_run ( ctx, "out of memory" );

		out = storage::text_value ( kept, made.size );

		return true;

	}

}
