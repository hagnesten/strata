#include "studio/app/studio.hh"

namespace studio {

	bool settings_file ( base::text& out, const char* name ) {

		if ( !base::environment_path ( "APPDATA", out ) )
			return false;

		out.add ( "\\Strata" );

		if ( !base::make_folder ( out.value ( ) ) )
			return false;

		out.add ( '\\' ).add ( name );

		return true;

	}

	bool documents_folder ( base::text& out ) {

		if ( !base::environment_path ( "USERPROFILE", out ) )
			return false;

		out.add ( "\\Documents\\Strata" );

		return base::make_folder ( out.value ( ) );

	}

	void parent_folder ( const char* path, base::text& out ) {

		size_t cut = base::length ( path );

		while ( cut && path [ cut - 1 ] != '\\' && path [ cut - 1 ] != '/' )
			cut--;

		if ( cut > 1 && path [ cut - 2 ] != ':' )
			cut--;

		out.clear ( );
		out.add ( path, cut );

	}

	void file_name_of ( const char* path, base::text& out ) {

		const char* name = path;

		for ( const char* at = path; *at; at++ )
			if ( *at == '\\' || *at == '/' )
				name = at + 1;

		out.clear ( );
		out.add ( name );

	}

	static void save_recent ( studio_state& app ) {

		base::text path;
		base::text body;

		if ( app.capturing || !settings_file ( path, RECENT_FILE ) )
			return;

		for ( uint32_t i = 0; i < app.recent.count; i++ )
			body.add ( app.recent.paths [ i ] ).add ( '\n' );

		base::write_whole ( path.value ( ), body.value ( ), body.size );

	}

	void remember_file ( studio_state& app, const char* path ) {

		recent_files& recent = app.recent;
		uint32_t found = recent.count;

		for ( uint32_t i = 0; i < recent.count; i++ )
			if ( base::same_folded ( recent.paths [ i ], path ) )
				found = i;

		if ( found == recent.count && recent.count < STUDIO_RECENT )
			recent.count++;

		if ( found == recent.count )
			found = recent.count - 1;

		for ( uint32_t i = found; i > 0; i-- )
			base::copy_text ( recent.paths [ i ], FILE_PATH_ROOM, recent.paths [ i - 1 ] );

		base::copy_text ( recent.paths [ 0 ], FILE_PATH_ROOM, path );
		save_recent ( app );

	}

	void load_settings ( studio_state& app ) {

		base::text path;
		base::text body;

		app.recent.count = 0;

		if ( settings_file ( path, RECENT_FILE ) && base::read_whole ( path.value ( ), body ) ) {

			size_t start = 0;

			for ( size_t i = 0; i <= body.size && app.recent.count < STUDIO_RECENT; i++ ) {

				if ( i < body.size && body.data [ i ] != '\n' && body.data [ i ] != '\r' )
					continue;

				if ( i > start ) {

					base::copy_span ( app.recent.paths [ app.recent.count ], FILE_PATH_ROOM, body.value ( ) + start, i - start );

					if ( base::path_exists ( app.recent.paths [ app.recent.count ] ) )
						app.recent.count++;

				}

				start = i + 1;

			}

		}

		load_library ( app );

	}

}