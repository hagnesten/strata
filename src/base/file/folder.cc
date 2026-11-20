#include "base/file/folder.hh"
#include "base/sort/sort.hh"
#include "base/platform/os.hh"

namespace base {

	static bool add_entry ( folder_list& out, const folder_entry& entry ) {

		if ( out.count == out.room ) {

			uint32_t grown = out.room ? out.room * 2 : 64;
			folder_entry* made = make < folder_entry > ( grown );

			if ( !made )
				return false;

			if ( out.items ) {

				copy ( made, out.items, sizeof ( folder_entry ) * out.count );
				release ( out.items );

			}

			out.items = made;
			out.room = grown;

		}

		out.items [ out.count++ ] = entry;

		return true;

	}

	static int32_t compare_entries ( void* context, const void* a, const void* b ) {

		( void ) context;

		const folder_entry& x = *static_cast < const folder_entry* > ( a );
		const folder_entry& y = *static_cast < const folder_entry* > ( b );

		if ( x.directory != y.directory )
			return x.directory ? -1 : 1;

		for ( uint32_t i = 0; i < FOLDER_NAME_ROOM; i++ ) {

			char p = lower ( x.name [ i ] );
			char q = lower ( y.name [ i ] );

			if ( p != q )
				return static_cast < uint8_t > ( p ) < static_cast < uint8_t > ( q ) ? -1 : 1;

			if ( !p )
				break;

		}

		return 0;

	}

	void forget_list ( folder_list& list ) {

		release ( list.items );
		list = { };

	}

	static bool visit_entry ( void* context, const char* name, bool directory, uint64_t size ) {

		folder_entry entry = { };

		if ( length ( name ) >= FOLDER_NAME_ROOM )
			return true;

		copy_text ( entry.name, FOLDER_NAME_ROOM, name );
		entry.directory = directory;
		entry.size = size;

		return add_entry ( *static_cast < folder_list* > ( context ), entry );

	}

	bool list_folder ( const char* path, folder_list& out ) {

		forget_list ( out );

		if ( !os::list_folder ( path, visit_entry, &out ) )
			return false;

		if ( !out.count )
			return true;

		folder_entry** order = make < folder_entry* > ( out.count );
		folder_entry* sorted = make < folder_entry > ( out.room );

		if ( !order || !sorted ) {

			release ( order );
			release ( sorted );

			return true;

		}

		for ( uint32_t i = 0; i < out.count; i++ )
			order [ i ] = &out.items [ i ];

		sort_pointers ( reinterpret_cast < void** > ( order ), out.count, compare_entries, nullptr );

		for ( uint32_t i = 0; i < out.count; i++ )
			sorted [ i ] = *order [ i ];

		release ( order );
		release ( out.items );
		out.items = sorted;

		return true;

	}

	bool list_drives ( folder_list& out ) {

		forget_list ( out );

		return os::list_roots ( visit_entry, &out );

	}

	bool make_folder ( const char* path ) {

		return os::make_folder ( path );

	}

	bool read_whole ( const char* path, text& out ) {

		file_handle file;

		out.clear ( );

		if ( !open_reading ( file, path ) )
			return false;

		uint64_t size = file_size ( file );
		bool fine = size < 0x40000000ull && out.reserve ( static_cast < size_t > ( size ) + 1 );

		if ( fine && size ) {

			fine = read_at ( file, 0, out.data, static_cast < size_t > ( size ) );

			if ( fine ) {

				out.size = static_cast < size_t > ( size );
				out.data [ out.size ] = 0;

			}

		}

		close_file ( file );

		return fine;

	}

	bool copy_file ( const char* from, const char* to ) {

		file_handle source;
		file_handle target;

		if ( !open_reading ( source, from ) )
			return false;

		if ( !open_file ( target, to, true ) ) {

			close_file ( source );

			return false;

		}

		uint64_t size = file_size ( source );
		uint64_t at = 0;
		size_t room = 1u << 20;
		char* chunk = make < char > ( room );
		bool fine = chunk && truncate_file ( target, 0 );

		while ( fine && at < size ) {

			size_t step = static_cast < size_t > ( size - at < room ? size - at : room );

			fine = read_at ( source, at, chunk, step ) && write_at ( target, at, chunk, step );
			at += step;

		}

		fine = fine && sync_file ( target );
		release ( chunk );
		close_file ( source );
		close_file ( target );

		return fine;

	}

	bool save_file ( const char* path, const char* data, size_t size ) {

		text temporary;

		temporary.add ( path ).add ( ".saving" );

		if ( !write_whole ( temporary.value ( ), data, size ) )
			return false;

		if ( replace_path ( temporary.value ( ), path ) )
			return true;

		erase_path ( temporary.value ( ) );

		return false;

	}

	bool save_private_file ( const char* path, const char* data, size_t size ) {

		text temporary;

		temporary.add ( path ).add ( ".saving" );

		if ( !write_whole ( temporary.value ( ), "", 0 ) || !os::restrict_file ( temporary.value ( ) ) || !write_whole ( temporary.value ( ), data, size ) ) {

			erase_path ( temporary.value ( ) );

			return false;

		}

		if ( replace_path ( temporary.value ( ), path ) )
			return true;

		erase_path ( temporary.value ( ) );

		return false;

	}

	bool write_whole ( const char* path, const char* data, size_t size ) {

		file_handle file;

		if ( !open_file ( file, path, true ) )
			return false;

		bool fine = truncate_file ( file, 0 ) && ( !size || write_at ( file, 0, data, size ) ) && sync_file ( file );

		close_file ( file );

		return fine;

	}

	bool environment_path ( const char* name, text& out ) {

		return os::environment ( name, out );

	}

}