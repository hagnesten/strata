#include "engine/value/interval.hh"

#define INTERVAL_DAY_MICROS 86400000000ll
#define INTERVAL_HOUR_MICROS 3600000000ll
#define INTERVAL_MINUTE_MICROS 60000000ll
#define INTERVAL_SECOND_MICROS 1000000ll

namespace engine {

	static bool letter ( uint8_t c ) {

		return ( c >= 'a' && c <= 'z' ) || ( c >= 'A' && c <= 'Z' );

	}

	static bool numeral ( uint8_t c ) {

		return c >= '0' && c <= '9';

	}

	static bool unit_is ( const uint8_t* word, size_t size, const char* const* names ) {

		for ( const char* const* name = names; *name; name++ )
			if ( base::length ( *name ) == size && base::same_folded_size ( reinterpret_cast < const char* > ( word ), *name, size ) )
				return true;

		return false;

	}

	static int64_t round_micros ( double value ) {

		return static_cast < int64_t > ( value < 0 ? value - 0.5 : value + 0.5 );

	}

	static void add_unit ( interval_parts& out, double amount, const uint8_t* word, size_t size, bool& known ) {

		static const char* const years [ ] = { "y", "yr", "yrs", "year", "years", nullptr };
		static const char* const months [ ] = { "mon", "mons", "month", "months", nullptr };
		static const char* const weeks [ ] = { "w", "week", "weeks", nullptr };
		static const char* const days [ ] = { "d", "day", "days", nullptr };
		static const char* const hours [ ] = { "h", "hr", "hrs", "hour", "hours", nullptr };
		static const char* const minutes [ ] = { "m", "min", "mins", "minute", "minutes", nullptr };
		static const char* const seconds [ ] = { "s", "sec", "secs", "second", "seconds", nullptr };
		static const char* const millis [ ] = { "ms", "msec", "msecs", "millisecond", "milliseconds", nullptr };
		static const char* const micros [ ] = { "us", "usec", "usecs", "microsecond", "microseconds", nullptr };
		static const char* const decades [ ] = { "decade", "decades", nullptr };
		static const char* const centuries [ ] = { "c", "century", "centuries", nullptr };
		static const char* const millennia [ ] = { "millennium", "millennia", nullptr };

		known = true;

		double month_count = 0;
		double day_count = 0;
		double micro_count = 0;

		if ( unit_is ( word, size, years ) )
			month_count = amount * 12;
		else if ( unit_is ( word, size, decades ) )
			month_count = amount * 120;
		else if ( unit_is ( word, size, centuries ) )
			month_count = amount * 1200;
		else if ( unit_is ( word, size, millennia ) )
			month_count = amount * 12000;
		else if ( unit_is ( word, size, months ) )
			month_count = amount;
		else if ( unit_is ( word, size, weeks ) )
			day_count = amount * 7;
		else if ( unit_is ( word, size, days ) )
			day_count = amount;
		else if ( unit_is ( word, size, hours ) )
			micro_count = amount * INTERVAL_HOUR_MICROS;
		else if ( unit_is ( word, size, minutes ) )
			micro_count = amount * INTERVAL_MINUTE_MICROS;
		else if ( unit_is ( word, size, seconds ) )
			micro_count = amount * INTERVAL_SECOND_MICROS;
		else if ( unit_is ( word, size, millis ) )
			micro_count = amount * 1000;
		else if ( unit_is ( word, size, micros ) )
			micro_count = amount;
		else {

			known = false;

			return;

		}

		int64_t whole_months = static_cast < int64_t > ( month_count );

		out.months += whole_months;
		day_count += ( month_count - static_cast < double > ( whole_months ) ) * 30;

		int64_t whole_days = static_cast < int64_t > ( day_count );

		out.days += whole_days;
		micro_count += ( day_count - static_cast < double > ( whole_days ) ) * INTERVAL_DAY_MICROS;
		out.micros += round_micros ( micro_count );

	}

	static bool read_amount ( const uint8_t*& at, const uint8_t* end, double& out ) {

		bool negative = false;

		if ( at < end && ( *at == '-' || *at == '+' ) ) {

			negative = *at == '-';
			at++;

		}

		if ( at >= end || ( !numeral ( *at ) && *at != '.' ) )
			return false;

		double value = 0;
		bool any = false;

		while ( at < end && numeral ( *at ) ) {

			value = value * 10 + ( *at - '0' );
			at++;
			any = true;

		}

		if ( at < end && *at == '.' ) {

			at++;

			double scale = 0.1;

			while ( at < end && numeral ( *at ) ) {

				value += ( *at - '0' ) * scale;
				scale /= 10;
				at++;
				any = true;

			}

		}

		out = negative ? -value : value;

		return any;

	}

	static bool read_clock ( const uint8_t*& at, const uint8_t* end, int64_t& out ) {

		bool negative = false;

		if ( at < end && ( *at == '-' || *at == '+' ) ) {

			negative = *at == '-';
			at++;

		}

		int64_t parts [ 2 ] = { 0, 0 };
		uint32_t count = 0;

		for ( ;; ) {

			if ( at >= end || !numeral ( *at ) )
				return false;

			int64_t value = 0;

			while ( at < end && numeral ( *at ) ) {

				value = value * 10 + ( *at - '0' );
				at++;

			}

			if ( count < 2 )
				parts [ count ] = value;

			count++;

			if ( count < 3 && at < end && *at == ':' ) {

				at++;

				continue;

			}

			int64_t seconds = 0;
			int64_t fraction = 0;

			if ( count == 3 ) {

				seconds = value;

				if ( at < end && *at == '.' ) {

					at++;

					int64_t scale = 100000;

					while ( at < end && numeral ( *at ) ) {

						fraction += ( *at - '0' ) * scale;
						scale /= 10;
						at++;

					}

				}

			} else if ( count != 2 )
				return false;

			int64_t total = parts [ 0 ] * INTERVAL_HOUR_MICROS + parts [ 1 ] * INTERVAL_MINUTE_MICROS + seconds * INTERVAL_SECOND_MICROS + fraction;

			out = negative ? -total : total;

			return true;

		}

	}

	static bool clock_ahead ( const uint8_t* at, const uint8_t* end ) {

		if ( at < end && ( *at == '-' || *at == '+' ) )
			at++;

		while ( at < end && numeral ( *at ) )
			at++;

		return at < end && *at == ':';

	}

	static bool parse_iso ( const uint8_t* at, const uint8_t* end, interval_parts& out ) {

		bool time = false;

		at++;

		while ( at < end ) {

			if ( *at == 'T' || *at == 't' ) {

				time = true;
				at++;

				continue;

			}

			double amount = 0;

			if ( !read_amount ( at, end, amount ) || at >= end )
				return false;

			char unit = static_cast < char > ( base::upper ( static_cast < char > ( *at++ ) ) );
			const char* word = unit == 'Y' ? "years" : unit == 'W' ? "weeks" : unit == 'D' ? "days" : unit == 'H' ? "hours" : unit == 'S' ? "seconds" : unit == 'M' ? ( time ? "minutes" : "months" ) : nullptr;
			bool known = false;

			if ( !word )
				return false;

			add_unit ( out, amount, reinterpret_cast < const uint8_t* > ( word ), base::length ( word ), known );

		}

		return true;

	}

	bool parse_interval ( const uint8_t* text, size_t size, interval_parts& out ) {

		out = { };

		const uint8_t* at = text;
		const uint8_t* end = text + size;

		while ( at < end && ( *at == ' ' || *at == '\t' ) )
			at++;

		while ( end > at && ( end [ -1 ] == ' ' || end [ -1 ] == '\t' ) )
			end--;

		if ( at >= end )
			return false;

		if ( *at == 'P' || *at == 'p' )
			return parse_iso ( at, end, out );

		if ( *at == '@' )
			at++;

		bool any = false;
		bool ago = false;

		while ( at < end ) {

			while ( at < end && ( *at == ' ' || *at == '\t' ) )
				at++;

			if ( at >= end )
				break;

			if ( letter ( *at ) ) {

				const uint8_t* word = at;

				while ( at < end && letter ( *at ) )
					at++;

				if ( static_cast < size_t > ( at - word ) == 3 && base::same_folded_size ( reinterpret_cast < const char* > ( word ), "ago", 3 ) ) {

					ago = true;

					continue;

				}

				return false;

			}

			if ( clock_ahead ( at, end ) ) {

				int64_t clock = 0;

				if ( !read_clock ( at, end, clock ) )
					return false;

				out.micros += clock;
				any = true;

				continue;

			}

			double amount = 0;

			if ( !read_amount ( at, end, amount ) )
				return false;

			while ( at < end && ( *at == ' ' || *at == '\t' ) )
				at++;

			if ( at < end && letter ( *at ) ) {

				const uint8_t* word = at;

				while ( at < end && letter ( *at ) )
					at++;

				bool known = false;

				add_unit ( out, amount, word, static_cast < size_t > ( at - word ), known );

				if ( !known )
					return false;

			} else if ( at < end && clock_ahead ( at, end ) ) {

				bool known = false;

				add_unit ( out, amount, reinterpret_cast < const uint8_t* > ( "days" ), 4, known );

			} else {

				bool known = false;

				add_unit ( out, amount, reinterpret_cast < const uint8_t* > ( "seconds" ), 7, known );

			}

			any = true;

		}

		if ( ago ) {

			out.months = -out.months;
			out.days = -out.days;
			out.micros = -out.micros;

		}

		return any;

	}

	static void add_count ( base::text& out, int64_t value, const char* one, const char* many ) {

		if ( out.size )
			out.add ( ' ' );

		out.signed_number ( value ).add ( ' ' ).add ( value == 1 ? one : many );

	}

	void write_interval ( const interval_parts& parts, base::text& out ) {

		base::text made;
		int64_t years = parts.months / 12;
		int64_t months = parts.months % 12;

		if ( years )
			add_count ( made, years, "year", "years" );

		if ( months )
			add_count ( made, months, "mon", "mons" );

		if ( parts.days )
			add_count ( made, parts.days, "day", "days" );

		if ( parts.micros || !made.size ) {

			int64_t micros = parts.micros;

			if ( made.size )
				made.add ( ' ' );

			if ( micros < 0 ) {

				made.add ( '-' );
				micros = -micros;

			}

			int64_t hours = micros / INTERVAL_HOUR_MICROS;
			int64_t minutes = micros / INTERVAL_MINUTE_MICROS % 60;
			int64_t seconds = micros / INTERVAL_SECOND_MICROS % 60;
			int64_t fraction = micros % INTERVAL_SECOND_MICROS;

			if ( hours < 10 )
				made.add ( '0' );

			made.number ( static_cast < uint64_t > ( hours ) ).add ( ':' );

			if ( minutes < 10 )
				made.add ( '0' );

			made.number ( static_cast < uint64_t > ( minutes ) ).add ( ':' );

			if ( seconds < 10 )
				made.add ( '0' );

			made.number ( static_cast < uint64_t > ( seconds ) );

			if ( fraction ) {

				char digits [ 7 ];
				int32_t last = 5;

				for ( int32_t i = 5; i >= 0; i-- ) {

					digits [ i ] = static_cast < char > ( '0' + fraction % 10 );
					fraction /= 10;

				}

				while ( last > 0 && digits [ last ] == '0' )
					last--;

				made.add ( '.' ).add ( digits, static_cast < size_t > ( last + 1 ) );

			}

		}

		out.add ( made.value ( ), made.size );

	}

	static int64_t month_length ( int64_t year, int64_t month ) {

		static const int64_t lengths [ ] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };

		if ( month == 2 && ( ( year % 4 == 0 && year % 100 != 0 ) || year % 400 == 0 ) )
			return 29;

		return lengths [ month - 1 ];

	}

	void shift_moment ( moment_parts& moment, const interval_parts& by, int64_t sign ) {

		if ( by.months ) {

			int64_t year = 0;
			int64_t month = 0;
			int64_t day = 0;

			moment_civil_from_days ( moment.days, year, month, day );

			int64_t total = year * 12 + ( month - 1 ) + sign * by.months;
			int64_t new_year = total >= 0 ? total / 12 : ( total - 11 ) / 12;
			int64_t new_month = total - new_year * 12 + 1;

			day = base::min ( day, month_length ( new_year, new_month ) );
			moment.days = moment_days_from_civil ( new_year, new_month, day );

		}

		moment.days += sign * by.days;
		moment.micros += sign * by.micros;

		int64_t carry = moment.micros >= 0 ? moment.micros / INTERVAL_DAY_MICROS : ( moment.micros - INTERVAL_DAY_MICROS + 1 ) / INTERVAL_DAY_MICROS;

		moment.days += carry;
		moment.micros -= carry * INTERVAL_DAY_MICROS;
		moment.has_time = true;

	}

	interval_parts moment_gap ( const moment_parts& later, const moment_parts& earlier ) {

		int64_t total = ( later.days - earlier.days ) * INTERVAL_DAY_MICROS + ( later.micros - earlier.micros );
		interval_parts out = { };

		out.days = total / INTERVAL_DAY_MICROS;
		out.micros = total % INTERVAL_DAY_MICROS;

		return out;

	}

	int64_t interval_key ( const interval_parts& parts ) {

		return ( parts.months * 30 + parts.days ) * INTERVAL_DAY_MICROS + parts.micros;

	}

	double interval_epoch ( const interval_parts& parts ) {

		double years = static_cast < double > ( parts.months / 12 );
		double months = static_cast < double > ( parts.months % 12 );

		return years * 365.25 * 86400.0 + months * 30.0 * 86400.0 + static_cast < double > ( parts.days ) * 86400.0 + static_cast < double > ( parts.micros ) / 1000000.0;

	}

	static bool field_is ( const char* field, size_t size, const char* name ) {

		return base::length ( name ) == size && base::same_folded_size ( field, name, size );

	}

	bool interval_field ( const char* field, size_t size, const interval_parts& parts, double& out ) {

		double seconds = static_cast < double > ( parts.micros % INTERVAL_MINUTE_MICROS ) / 1000000.0;

		if ( field_is ( field, size, "epoch" ) )
			out = interval_epoch ( parts );
		else if ( field_is ( field, size, "year" ) || field_is ( field, size, "years" ) )
			out = static_cast < double > ( parts.months / 12 );
		else if ( field_is ( field, size, "month" ) || field_is ( field, size, "months" ) )
			out = static_cast < double > ( parts.months % 12 );
		else if ( field_is ( field, size, "day" ) || field_is ( field, size, "days" ) )
			out = static_cast < double > ( parts.days );
		else if ( field_is ( field, size, "hour" ) || field_is ( field, size, "hours" ) )
			out = static_cast < double > ( parts.micros / INTERVAL_HOUR_MICROS );
		else if ( field_is ( field, size, "minute" ) || field_is ( field, size, "minutes" ) )
			out = static_cast < double > ( parts.micros / INTERVAL_MINUTE_MICROS % 60 );
		else if ( field_is ( field, size, "second" ) || field_is ( field, size, "seconds" ) )
			out = seconds;
		else if ( field_is ( field, size, "millisecond" ) || field_is ( field, size, "milliseconds" ) )
			out = seconds * 1000.0;
		else if ( field_is ( field, size, "microsecond" ) || field_is ( field, size, "microseconds" ) )
			out = seconds * 1000000.0;
		else
			return false;

		return true;

	}

	interval_parts scale_interval ( const interval_parts& parts, double factor ) {

		double months = static_cast < double > ( parts.months ) * factor;
		double days = static_cast < double > ( parts.days ) * factor;
		double micros = static_cast < double > ( parts.micros ) * factor;
		interval_parts out = { };

		out.months = static_cast < int64_t > ( months );
		days += ( months - static_cast < double > ( out.months ) ) * 30;
		out.days = static_cast < int64_t > ( days );
		micros += ( days - static_cast < double > ( out.days ) ) * INTERVAL_DAY_MICROS;
		out.micros = round_micros ( micros );

		return out;

	}

}
