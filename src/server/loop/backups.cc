#include "server/server.hh"
#include "base/file/folder.hh"

namespace server {

	static void base_name ( const char* path, base::text& out ) {

		const char* start = path;

		for ( const char* at = path; *at; at++ )
			if ( *at == '/' || *at == '\\' )
				start = at + 1;

		out.add ( start );

	}

	static int32_t order_names ( const char* a, const char* b ) {

		while ( *a && *a == *b ) {

			a++;
			b++;

		}

		return static_cast < uint8_t > ( *a ) - static_cast < uint8_t > ( *b );

	}

	static void prune ( server_state& state ) {

		base::folder_list list = { };
		base::text prefix;

		base_name ( state.settings.path, prefix );
		prefix.add ( '-' );

		if ( !base::list_folder ( state.settings.backup_folder, list ) )
			return;

		const char* names [ 4096 ];
		uint32_t count = 0;

		for ( uint32_t i = 0; i < list.count && count < 4096; i++ ) {

			const char* name = list.items [ i ].name;
			size_t length = base::length ( name );

			if ( !list.items [ i ].directory && base::starts_with ( name, prefix.value ( ) ) && length > 4 && base::same ( name + length - 4, ".sdb" ) )
				names [ count++ ] = name;

		}

		for ( uint32_t i = 1; i < count; i++ )
			for ( uint32_t j = i; j > 0 && order_names ( names [ j - 1 ], names [ j ] ) > 0; j-- ) {

				const char* swap = names [ j ];

				names [ j ] = names [ j - 1 ];
				names [ j - 1 ] = swap;

			}

		for ( uint32_t i = 0; count > state.settings.backup_keep && i < count - state.settings.backup_keep; i++ ) {

			static const char* const endings [ ] = { "", ".users", ".grants" };

			for ( const char* ending : endings ) {

				base::text path;

				path.add ( state.settings.backup_folder ).add ( base::os::separator ( ) ).add ( names [ i ] ).add ( ending );

				if ( base::path_exists ( path.value ( ) ) )
					base::erase_path ( path.value ( ) );

			}

			base::text line;

			line.add ( "removed the old backup " ).add ( names [ i ] );
			server_log ( state, line.value ( ) );

		}

		base::forget_list ( list );

	}

	void tend_backups ( server_state& state, uint64_t now ) {

		if ( !state.settings.backup_folder || !state.settings.backup_minutes )
			return;

		if ( state.backup_process ) {

			int32_t code = 0;

			if ( !base::os::process_finished ( state.backup_process, code ) )
				return;

			state.backup_process = nullptr;

			base::text line;

			if ( code == 0 ) {

				line.add ( "backup finished: " ).add ( state.backup_target );
				server_log ( state, line.value ( ) );
				prune ( state );

			} else {

				line.add ( "backup FAILED with code " ).signed_number ( code ).add ( ": " ).add ( state.backup_target );
				server_log ( state, line.value ( ) );

			}

			return;

		}

		if ( now < state.next_backup )
			return;

		state.next_backup = now + static_cast < uint64_t > ( state.settings.backup_minutes ) * 60000;
		base::make_folder ( state.settings.backup_folder );

		base::text program;
		base::text target;

		if ( !base::os::own_program ( program ) )
			return;

		target.add ( state.settings.backup_folder ).add ( base::os::separator ( ) );
		base_name ( state.settings.path, target );
		target.add ( '-' );
		time_stamp ( target, true );
		target.add ( ".sdb" );
		base::copy_text ( state.backup_target, sizeof ( state.backup_target ), target.value ( ) );

		const char* arguments [ 3 ] = { "--backup", state.settings.path, state.backup_target };
		base::text line;

		if ( base::os::start_process ( program.value ( ), arguments, 3, state.backup_process ) )
			line.add ( "backup started: " ).add ( state.backup_target );
		else
			line.add ( "backup could not start: " ).add ( state.backup_target );

		server_log ( state, line.value ( ) );

	}

}
