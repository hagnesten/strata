#include "engine/value/moment.hh"

#define MOMENT_DAY_MICROS 86400000000ll

namespace engine {

	int64_t moment_days_from_civil ( int64_t year, int64_t month, int64_t day ) {

		year -= month <= 2 ? 1 : 0;

		int64_t era = ( year >= 0 ? year : year - 399 ) / 400;
		int64_t of_era = year - era * 400;
		int64_t of_year = ( 153 * ( month + ( month > 2 ? -3 : 9 ) ) + 2 ) / 5 + day - 1;
		int64_t of_cycle = of_era * 365 + of_era / 4 - of_era / 100 + of_year;

		return era * 146097 + of_cycle - 719468;

	}

	void moment_civil_from_days ( int64_t days, int64_t& year, int64_t& month, int64_t& day ) {

		days += 719468;

		int64_t era = ( days >= 0 ? days : days - 146096 ) / 146097;
		int64_t of_era = days - era * 146097;
		int64_t of_cycle = ( of_era - of_era / 1460 + of_era / 36524 - of_era / 146096 ) / 365;
		int64_t of_year = of_era - ( 365 * of_cycle + of_cycle / 4 - of_cycle / 100 );
		int64_t shifted = ( 5 * of_year + 2 ) / 153;

		day = of_year - ( 153 * shifted + 2 ) / 5 + 1;
		month = shifted + ( shifted < 10 ? 3 : -9 );
		year = of_cycle + era * 400 + ( month <= 2 ? 1 : 0 );

	}

	moment_kind moment_type ( const char* type ) {

		if ( !type )
			return MOMENT_NONE;

		if ( base::find_folded ( type, "timestamptz" ) || ( base::find_folded ( type, "timestamp" ) && base::find_folded ( type, "with time zone" ) && !base::find_folded ( type, "without" ) ) )
			return MOMENT_ZONED;

		if ( base::find_folded ( type, "timestamp" ) || base::find_folded ( type, "datetime" ) )
			return MOMENT_TIMESTAMP;

		if ( base::find_folded ( type, "date" ) )
			return MOMENT_DATE;

		if ( base::find_folded ( type, "timetz" ) || base::find_folded ( type, "interval" ) )
			return MOMENT_NONE;

		if ( base::find_folded ( type, "time" ) )
			return MOMENT_TIME;

		return MOMENT_NONE;

	}

	static bool take_digits ( const uint8_t*& at, const uint8_t* end, uint32_t low, uint32_t high, int64_t& out ) {

		uint32_t count = 0;

		out = 0;

		while ( at < end && *at >= '0' && *at <= '9' && count < high ) {

			out = out * 10 + ( *at - '0' );
			at++;
			count++;

		}

		return count >= low;

	}

	static bool take_clock ( const uint8_t*& at, const uint8_t* end, int64_t& micros ) {

		int64_t hour = 0;
		int64_t minute = 0;
		int64_t second = 0;
		int64_t fraction = 0;

		if ( !take_digits ( at, end, 1, 2, hour ) || at >= end || *at++ != ':' || !take_digits ( at, end, 2, 2, minute ) )
			return false;

		if ( at < end && *at == ':' ) {

			at++;

			if ( !take_digits ( at, end, 2, 2, second ) )
				return false;

			if ( at < end && *at == '.' ) {

				at++;

				uint32_t count = 0;
				bool round = false;

				while ( at < end && *at >= '0' && *at <= '9' ) {

					if ( count < 6 )
						fraction = fraction * 10 + ( *at - '0' );
					else if ( count == 6 )
						round = *at >= '5';

					count++;
					at++;

				}

				if ( !count )
					return false;

				for ( uint32_t i = count; i < 6; i++ )
					fraction *= 10;

				if ( round )
					fraction++;

			}

		}

		if ( hour > 24 || minute > 59 || second > 60 || ( hour == 24 && ( minute || second || fraction ) ) )
			return false;

		micros = ( ( hour * 60 + minute ) * 60 + second ) * 1000000 + fraction;

		return true;

	}

	bool parse_moment ( const uint8_t* text, size_t size, bool zoned, moment_parts& out ) {

		const uint8_t* at = text;
		const uint8_t* end = text + size;

		out = { };

		while ( at < end && ( *at == ' ' || *at == '\t' ) )
			at++;

		while ( end > at && ( end [ -1 ] == ' ' || end [ -1 ] == '\t' ) )
			end--;

		const uint8_t* start = at;
		int64_t year = 0;
		int64_t month = 0;
		int64_t day = 0;

		if ( take_digits ( at, end, 4, 6, year ) && at < end && *at == '-' ) {

			at++;

			if ( !take_digits ( at, end, 1, 2, month ) || at >= end || *at++ != '-' || !take_digits ( at, end, 1, 2, day ) )
				return false;

			if ( month < 1 || month > 12 || day < 1 || day > 31 )
				return false;

			int64_t days = moment_days_from_civil ( year, month, day );
			int64_t check_year = 0;
			int64_t check_month = 0;
			int64_t check_day = 0;

			moment_civil_from_days ( days, check_year, check_month, check_day );

			if ( check_month != month || check_day != day )
				return false;

			out.days = days;
			out.has_date = true;

			if ( at < end && ( *at == ' ' || *at == 'T' || *at == 't' ) ) {

				at++;

				while ( at < end && *at == ' ' )
					at++;

				if ( !take_clock ( at, end, out.micros ) )
					return false;

				out.has_time = true;

			}

		} else {

			at = start;

			if ( !take_clock ( at, end, out.micros ) )
				return false;

			out.has_time = true;

		}

		while ( at < end && *at == ' ' )
			at++;

		if ( at == end )
			return true;

		int64_t offset = 0;

		if ( ( *at == 'Z' || *at == 'z' ) && at + 1 == end )
			offset = 0;
		else if ( end - at == 3 && base::same_folded_size ( reinterpret_cast < const char* > ( at ), "utc", 3 ) )
			offset = 0;
		else if ( *at == '+' || *at == '-' ) {

			bool behind = *at++ == '-';
			int64_t hours = 0;
			int64_t minutes = 0;

			if ( !take_digits ( at, end, 1, 2, hours ) )
				return false;

			if ( at < end && *at == ':' )
				at++;

			if ( at < end && !take_digits ( at, end, 2, 2, minutes ) )
				return false;

			if ( at != end || hours > 15 || minutes > 59 )
				return false;

			offset = ( hours * 60 + minutes ) * 60000000ll;
			offset = behind ? -offset : offset;

		} else
			return false;

		if ( zoned && out.has_date ) {

			int64_t total = out.days * MOMENT_DAY_MICROS + out.micros - offset;
			int64_t days = total >= 0 ? total / MOMENT_DAY_MICROS : ( total - MOMENT_DAY_MICROS + 1 ) / MOMENT_DAY_MICROS;

			out.days = days;
			out.micros = total - days * MOMENT_DAY_MICROS;

		}

		return true;

	}

	static void two ( base::text& out, int64_t value ) {

		out.add ( static_cast < char > ( '0' + value / 10 % 10 ) ).add ( static_cast < char > ( '0' + value % 10 ) );

	}

	static void write_clock ( base::text& out, int64_t micros ) {

		int64_t seconds = micros / 1000000;
		int64_t fraction = micros % 1000000;

		two ( out, seconds / 3600 );
		out.add ( ':' );
		two ( out, seconds / 60 % 60 );
		out.add ( ':' );
		two ( out, seconds % 60 );

		if ( fraction ) {

			char written [ 6 ];
			uint32_t used = 6;

			for ( int32_t i = 5; i >= 0; i-- ) {

				written [ i ] = static_cast < char > ( '0' + fraction % 10 );
				fraction /= 10;

			}

			while ( used > 1 && written [ used - 1 ] == '0' )
				used--;

			out.add ( '.' ).add ( written, used );

		}

	}

	void write_moment ( const moment_parts& parts, moment_kind kind, base::text& out ) {

		if ( kind != MOMENT_TIME ) {

			int64_t year = 0;
			int64_t month = 0;
			int64_t day = 0;

			moment_civil_from_days ( parts.days, year, month, day );

			if ( year < 1000 )
				out.add ( year < 10 ? "000" : year < 100 ? "00" : "0" );

			out.signed_number ( year ).add ( '-' );
			two ( out, month );
			out.add ( '-' );
			two ( out, day );

			if ( kind == MOMENT_DATE )
				return;

			out.add ( ' ' );

		}

		write_clock ( out, parts.micros );

	}

	bool canonical_moment ( const uint8_t* text, size_t size, moment_kind kind, base::text& out ) {

		moment_parts parts = { };

		if ( kind == MOMENT_NONE || !parse_moment ( text, size, kind == MOMENT_ZONED, parts ) )
			return false;

		if ( kind != MOMENT_TIME && !parts.has_date )
			return false;

		write_moment ( parts, kind, out );

		return true;

	}

	static bool field_is ( const char* field, size_t size, const char* name ) {

		return base::length ( name ) == size && base::same_folded_size ( field, name, size );

	}

	bool moment_field ( const char* field, size_t size, const moment_parts& parts, double& out ) {

		int64_t year = 0;
		int64_t month = 0;
		int64_t day = 0;

		moment_civil_from_days ( parts.days, year, month, day );

		int64_t seconds = parts.micros / 1000000;
		int64_t fraction = parts.micros % 1000000;

		if ( field_is ( field, size, "year" ) || field_is ( field, size, "years" ) )
			out = static_cast < double > ( year );
		else if ( field_is ( field, size, "month" ) || field_is ( field, size, "months" ) )
			out = static_cast < double > ( month );
		else if ( field_is ( field, size, "day" ) || field_is ( field, size, "days" ) )
			out = static_cast < double > ( day );
		else if ( field_is ( field, size, "hour" ) || field_is ( field, size, "hours" ) )
			out = static_cast < double > ( seconds / 3600 );
		else if ( field_is ( field, size, "minute" ) || field_is ( field, size, "minutes" ) )
			out = static_cast < double > ( seconds / 60 % 60 );
		else if ( field_is ( field, size, "second" ) || field_is ( field, size, "seconds" ) )
			out = static_cast < double > ( seconds % 60 ) + static_cast < double > ( fraction ) / 1000000.0;
		else if ( field_is ( field, size, "millisecond" ) || field_is ( field, size, "milliseconds" ) )
			out = static_cast < double > ( seconds % 60 ) * 1000.0 + static_cast < double > ( fraction ) / 1000.0;
		else if ( field_is ( field, size, "microsecond" ) || field_is ( field, size, "microseconds" ) )
			out = static_cast < double > ( seconds % 60 * 1000000 + fraction );
		else if ( field_is ( field, size, "dow" ) )
			out = static_cast < double > ( ( ( parts.days + 4 ) % 7 + 7 ) % 7 );
		else if ( field_is ( field, size, "isodow" ) ) {

			int64_t dow = ( ( parts.days + 4 ) % 7 + 7 ) % 7;

			out = static_cast < double > ( dow ? dow : 7 );

		} else if ( field_is ( field, size, "doy" ) )
			out = static_cast < double > ( parts.days - moment_days_from_civil ( year, 1, 1 ) + 1 );
		else if ( field_is ( field, size, "quarter" ) )
			out = static_cast < double > ( ( month - 1 ) / 3 + 1 );
		else if ( field_is ( field, size, "epoch" ) )
			out = static_cast < double > ( parts.days ) * 86400.0 + static_cast < double > ( parts.micros ) / 1000000.0;
		else if ( field_is ( field, size, "week" ) ) {

			int64_t dow = ( ( parts.days + 3 ) % 7 + 7 ) % 7;
			int64_t thursday = parts.days - dow + 3;
			int64_t thursday_year = 0;
			int64_t ignored_month = 0;
			int64_t ignored_day = 0;

			moment_civil_from_days ( thursday, thursday_year, ignored_month, ignored_day );
			out = static_cast < double > ( ( thursday - moment_days_from_civil ( thursday_year, 1, 1 ) ) / 7 + 1 );

		} else if ( field_is ( field, size, "decade" ) )
			out = static_cast < double > ( year / 10 );
		else if ( field_is ( field, size, "century" ) )
			out = static_cast < double > ( ( year + 99 ) / 100 );
		else
			return false;

		return true;

	}

	bool truncate_moment ( const char* field, size_t size, moment_parts& parts ) {

		int64_t year = 0;
		int64_t month = 0;
		int64_t day = 0;

		moment_civil_from_days ( parts.days, year, month, day );

		if ( field_is ( field, size, "microseconds" ) )
			return true;

		if ( field_is ( field, size, "milliseconds" ) )
			parts.micros -= parts.micros % 1000;
		else if ( field_is ( field, size, "second" ) )
			parts.micros -= parts.micros % 1000000;
		else if ( field_is ( field, size, "minute" ) )
			parts.micros -= parts.micros % 60000000;
		else if ( field_is ( field, size, "hour" ) )
			parts.micros -= parts.micros % 3600000000ll;
		else if ( field_is ( field, size, "day" ) )
			parts.micros = 0;
		else if ( field_is ( field, size, "week" ) ) {

			parts.days -= ( ( parts.days + 3 ) % 7 + 7 ) % 7;
			parts.micros = 0;

		} else if ( field_is ( field, size, "month" ) ) {

			parts.days = moment_days_from_civil ( year, month, 1 );
			parts.micros = 0;

		} else if ( field_is ( field, size, "quarter" ) ) {

			parts.days = moment_days_from_civil ( year, ( month - 1 ) / 3 * 3 + 1, 1 );
			parts.micros = 0;

		} else if ( field_is ( field, size, "year" ) ) {

			parts.days = moment_days_from_civil ( year, 1, 1 );
			parts.micros = 0;

		} else
			return false;

		return true;

	}

}
