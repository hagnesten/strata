#include "server/server.hh"
#include "base/file/file.hh"
#include "base/console/console.hh"

namespace server {

	static void users_path ( const char* path, base::text& out ) {

		out.clear ( );
		out.add ( path ).add ( ".users" );

	}

	static void add_hex ( base::text& out, const uint8_t* data, size_t size ) {

		for ( size_t i = 0; i < size; i++ )
			out.hex ( data [ i ], 2 );

	}

	static bool read_hex ( const char*& at, uint8_t* out, size_t size ) {

		for ( size_t i = 0; i < size; i++ ) {

			uint8_t value = 0;

			for ( uint32_t k = 0; k < 2; k++ ) {

				char c = *at++;

				value = static_cast < uint8_t > ( value << 4 );

				if ( c >= '0' && c <= '9' )
					value |= static_cast < uint8_t > ( c - '0' );
				else if ( c >= 'a' && c <= 'f' )
					value |= static_cast < uint8_t > ( c - 'a' + 10 );
				else
					return false;

			}

			out [ i ] = value;

		}

		return true;

	}

	static bool read_file ( const char* path, base::text& out ) {

		base::file_handle file;

		out.clear ( );

		if ( !base::path_exists ( path ) || !base::open_file ( file, path, false ) )
			return false;

		uint64_t size = base::file_size ( file );
		bool fine = size < 16 * 1024 * 1024 && out.reserve ( static_cast < size_t > ( size ) + 1 );

		if ( fine && size ) {

			fine = base::read_at ( file, 0, out.data, static_cast < size_t > ( size ) );
			out.size = static_cast < size_t > ( size );
			out.data [ out.size ] = 0;

		}

		base::close_file ( file );

		return fine;

	}

	static bool parse_users ( const char* text, server_user* users, uint32_t& count ) {

		const char* at = text;

		count = 0;

		while ( *at && count < SERVER_USERS ) {

			server_user& user = users [ count ];
			uint32_t length = 0;

			user = { };

			while ( *at && *at != '\t' && *at != '\n' && length + 1 < SERVER_NAME_ROOM )
				user.name [ length++ ] = *at++;

			if ( *at++ != '\t' || !read_hex ( at, user.salt, SERVER_SALT ) || *at++ != '\t' )
				return false;

			while ( *at >= '0' && *at <= '9' )
				user.iterations = user.iterations * 10 + static_cast < uint32_t > ( *at++ - '0' );

			if ( *at++ != '\t' || !read_hex ( at, user.stored, SHA256_BYTES ) || *at++ != '\t' || !read_hex ( at, user.check, SHA256_BYTES ) )
				return false;

			while ( *at == '\r' || *at == '\n' )
				at++;

			if ( length && user.iterations )
				count++;

		}

		return true;

	}

	bool make_user ( const char* name, const char* password, server_user& out ) {

		uint8_t salted [ SHA256_BYTES ];
		uint8_t client [ SHA256_BYTES ];

		out = { };
		base::copy_text ( out.name, sizeof ( out.name ), name );
		out.iterations = SERVER_ITERATIONS;

		if ( !base::os::random_bytes ( out.salt, SERVER_SALT ) )
			return false;

		base::pbkdf2_sha256 ( password, base::length ( password ), out.salt, SERVER_SALT, out.iterations, salted, SHA256_BYTES );
		base::hmac_sha256 ( salted, SHA256_BYTES, "Client Key", 10, client );
		base::sha256 ( client, SHA256_BYTES, out.stored );
		base::hmac_sha256 ( salted, SHA256_BYTES, "Server Key", 10, out.check );

		return true;

	}

	bool load_users ( server_state& state ) {

		base::text path;
		base::text content;

		users_path ( state.settings.path, path );
		state.user_count = 0;

		if ( !read_file ( path.value ( ), content ) )
			return false;

		return parse_users ( content.value ( ), state.users, state.user_count ) && state.user_count > 0;

	}

	bool find_user ( server_state& state, const char* name, server_user*& out ) {

		for ( uint32_t i = 0; i < state.user_count; i++ ) {

			if ( base::same ( state.users [ i ].name, name ) ) {

				out = &state.users [ i ];

				return true;

			}

		}

		out = nullptr;

		return false;

	}

	int32_t manage_user ( const char* path, const char* name, const char* password ) {

		server_user* users = base::make < server_user > ( SERVER_USERS + 1 );
		uint32_t count = 0;
		base::text file_path;
		base::text content;

		if ( !users )
			return 1;

		users_path ( path, file_path );

		if ( read_file ( file_path.value ( ), content ) && !parse_users ( content.value ( ), users, count ) ) {

			base::write_error ( "the users file is damaged\n" );
			base::release ( users );

			return 1;

		}

		uint32_t kept = 0;

		for ( uint32_t i = 0; i < count; i++ )
			if ( !base::same ( users [ i ].name, name ) )
				users [ kept++ ] = users [ i ];

		count = kept;

		if ( password && *password ) {

			if ( base::length ( name ) + 1 >= SERVER_NAME_ROOM || count == SERVER_USERS || !make_user ( name, password, users [ count ] ) ) {

				base::write_error ( "the user could not be added\n" );
				base::release ( users );

				return 1;

			}

			count++;

		}

		base::text written;

		for ( uint32_t i = 0; i < count; i++ ) {

			written.add ( users [ i ].name ).add ( '\t' );
			add_hex ( written, users [ i ].salt, SERVER_SALT );
			written.add ( '\t' ).number ( users [ i ].iterations ).add ( '\t' );
			add_hex ( written, users [ i ].stored, SHA256_BYTES );
			written.add ( '\t' );
			add_hex ( written, users [ i ].check, SHA256_BYTES );
			written.add ( '\n' );

		}

		base::file_handle file;
		bool fine = base::open_file ( file, file_path.value ( ), true );

		if ( fine ) {

			fine = base::truncate_file ( file, 0 ) && ( !written.size || base::write_at ( file, 0, written.value ( ), written.size ) ) && base::sync_file ( file );
			base::close_file ( file );

		}

		base::release ( users );

		if ( !fine ) {

			base::write_error ( "the users file could not be written\n" );

			return 1;

		}

		base::text line;

		line.add ( password && *password ? "saved the user " : "removed the user " ).add ( name ).add ( '\n' );
		base::write ( line );

		return 0;

	}

}
