#include "bench/postgres/wire.hh"
#include "base/platform/os.hh"

namespace bench {

	static bool keep ( char* out, const char* value, size_t size ) {

		if ( size >= WIRE_FIELD_ROOM )
			return false;

		base::copy_span ( out, WIRE_FIELD_ROOM, value, size );

		return true;

	}

	static void from_environment ( const char* name, char* out ) {

		base::text found;

		if ( base::os::environment ( name, found ) && found.size )
			keep ( out, found.value ( ), found.size );

	}

	static bool read_port ( const char* value, size_t size, uint16_t& out ) {

		uint32_t number = 0;

		if ( !size || size > 5 )
			return false;

		for ( size_t i = 0; i < size; i++ ) {

			if ( value [ i ] < '0' || value [ i ] > '9' )
				return false;

			number = number * 10 + static_cast < uint32_t > ( value [ i ] - '0' );

		}

		if ( !number || number > 65535 )
			return false;

		out = static_cast < uint16_t > ( number );

		return true;

	}

	static int32_t hex_digit ( char value ) {

		if ( value >= '0' && value <= '9' )
			return value - '0';

		if ( value >= 'a' && value <= 'f' )
			return value - 'a' + 10;

		if ( value >= 'A' && value <= 'F' )
			return value - 'A' + 10;

		return -1;

	}

	static bool keep_decoded ( char* out, const char* value, size_t size ) {

		size_t used = 0;

		for ( size_t i = 0; i < size; i++ ) {

			char item = value [ i ];

			if ( item == '%' && i + 2 < size && hex_digit ( value [ i + 1 ] ) >= 0 && hex_digit ( value [ i + 2 ] ) >= 0 ) {

				item = static_cast < char > ( hex_digit ( value [ i + 1 ] ) * 16 + hex_digit ( value [ i + 2 ] ) );
				i += 2;

			}

			if ( used + 1 >= WIRE_FIELD_ROOM )
				return false;

			out [ used++ ] = item;

		}

		out [ used ] = 0;

		return true;

	}

	static size_t find ( const char* value, size_t size, char wanted ) {

		for ( size_t i = 0; i < size; i++ )
			if ( value [ i ] == wanted )
				return i;

		return size;

	}

	static size_t find_last ( const char* value, size_t size, char wanted ) {

		for ( size_t i = size; i > 0; i-- )
			if ( value [ i - 1 ] == wanted )
				return i - 1;

		return size;

	}

	static bool read_address ( const char* value, wire_settings& out ) {

		size_t size = base::length ( value );
		size_t query = find ( value, size, '?' );
		size_t slash = find ( value, query, '/' );
		size_t at = find_last ( value, slash, '@' );
		const char* host = value;
		size_t host_size = slash;

		if ( at < slash ) {

			size_t colon = find ( value, at, ':' );

			if ( !keep_decoded ( out.user, value, colon ) )
				return false;

			if ( colon < at && !keep_decoded ( out.password, value + colon + 1, at - colon - 1 ) )
				return false;

			host = value + at + 1;
			host_size = slash - at - 1;

		}

		size_t colon = find_last ( host, host_size, ':' );

		if ( colon < host_size && host [ 0 ] != '[' ) {

			if ( !read_port ( host + colon + 1, host_size - colon - 1, out.port ) )
				return false;

			host_size = colon;

		}

		if ( host_size && !keep_decoded ( out.host, host, host_size ) )
			return false;

		if ( slash + 1 < query && !keep_decoded ( out.database, value + slash + 1, query - slash - 1 ) )
			return false;

		return true;

	}

	static bool read_pairs ( const char* value, wire_settings& out ) {

		const char* at = value;

		for ( ;; ) {

			while ( *at == ' ' || *at == '\t' )
				at++;

			if ( !*at )
				return true;

			const char* key = at;

			while ( *at && *at != '=' && *at != ' ' )
				at++;

			size_t key_size = static_cast < size_t > ( at - key );

			while ( *at == ' ' )
				at++;

			if ( *at != '=' )
				return false;

			at++;

			while ( *at == ' ' )
				at++;

			char item [ WIRE_FIELD_ROOM ];
			size_t used = 0;

			if ( *at == '\'' ) {

				at++;

				while ( *at && *at != '\'' ) {

					if ( *at == '\\' && at [ 1 ] )
						at++;

					if ( used + 1 >= WIRE_FIELD_ROOM )
						return false;

					item [ used++ ] = *at++;

				}

				if ( *at != '\'' )
					return false;

				at++;

			} else {

				while ( *at && *at != ' ' && *at != '\t' ) {

					if ( used + 1 >= WIRE_FIELD_ROOM )
						return false;

					item [ used++ ] = *at++;

				}

			}

			item [ used ] = 0;

			char name [ 16 ] = { };

			if ( key_size >= sizeof ( name ) )
				continue;

			base::copy_span ( name, sizeof ( name ), key, key_size );

			if ( base::same ( name, "host" ) || base::same ( name, "hostaddr" ) )
				keep ( out.host, item, used );
			else if ( base::same ( name, "port" ) ) {

				if ( !read_port ( item, used, out.port ) )
					return false;

			} else if ( base::same ( name, "user" ) )
				keep ( out.user, item, used );
			else if ( base::same ( name, "password" ) )
				keep ( out.password, item, used );
			else if ( base::same ( name, "dbname" ) || base::same ( name, "database" ) )
				keep ( out.database, item, used );

		}

	}

	bool read_settings ( const char* value, wire_settings& out ) {

		out = { };
		out.port = WIRE_DEFAULT_PORT;
		base::copy_text ( out.host, WIRE_FIELD_ROOM, "localhost" );
		base::copy_text ( out.user, WIRE_FIELD_ROOM, "postgres" );
		from_environment ( "PGHOST", out.host );
		from_environment ( "PGUSER", out.user );
		from_environment ( "PGPASSWORD", out.password );
		from_environment ( "PGDATABASE", out.database );

		base::text port;

		if ( base::os::environment ( "PGPORT", port ) && port.size && !read_port ( port.value ( ), port.size, out.port ) )
			return false;

		bool fine = true;

		if ( base::starts_with ( value, "postgresql://" ) )
			fine = read_address ( value + 13, out );
		else if ( base::starts_with ( value, "postgres://" ) )
			fine = read_address ( value + 11, out );
		else
			fine = read_pairs ( value, out );

		if ( !out.database [ 0 ] )
			base::copy_text ( out.database, WIRE_FIELD_ROOM, out.user );

		return fine;

	}

}
