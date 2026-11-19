#include "base/file/folder.hh"
#include "base/sort/sort.hh"

#include <windows.h>

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

	bool list_folder ( const char* path, folder_list& out ) {

		forget_list ( out );

		text pattern;
		wchar_t wide [ FILE_PATH_ROOM ];

		pattern.add ( path );

		if ( pattern.size && pattern.data [ pattern.size - 1 ] != '\\' )
			pattern.add ( '\\' );

		pattern.add ( '*' );

		if ( !wide_path ( pattern.value ( ), wide, FILE_PATH_ROOM ) )
			return false;

		WIN32_FIND_DATAW found;
		HANDLE search = FindFirstFileExW ( wide, FindExInfoBasic, &found, FindExSearchNameMatch, nullptr, FIND_FIRST_EX_LARGE_FETCH );

		if ( search == INVALID_HANDLE_VALUE )
			return false;

		bool fine = true;

		do {

			if ( found.cFileName [ 0 ] == '.' && ( !found.cFileName [ 1 ] || ( found.cFileName [ 1 ] == '.' && !found.cFileName [ 2 ] ) ) )
				continue;

			if ( found.dwFileAttributes & ( FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM ) )
				continue;

			folder_entry entry = { };

			if ( WideCharToMultiByte ( CP_UTF8, 0, found.cFileName, -1, entry.name, FOLDER_NAME_ROOM, nullptr, nullptr ) <= 0 )
				continue;

			entry.directory = ( found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY ) != 0;
			entry.size = static_cast < uint64_t > ( found.nFileSizeHigh ) << 32 | found.nFileSizeLow;
			fine = add_entry ( out, entry );

		} while ( fine && FindNextFileW ( search, &found ) );

		FindClose ( search );

		if ( !fine || !out.count )
			return fine;

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

		DWORD drives = GetLogicalDrives ( );

		for ( uint32_t d = 0; d < 26; d++ ) {

			if ( !( drives & ( 1u << d ) ) )
				continue;

			folder_entry entry = { };

			entry.name [ 0 ] = static_cast < char > ( 'A' + d );
			entry.name [ 1 ] = ':';
			entry.directory = true;

			if ( !add_entry ( out, entry ) )
				return false;

		}

		return true;

	}

	bool make_folder ( const char* path ) {

		wchar_t wide [ FILE_PATH_ROOM ];

		if ( !wide_path ( path, wide, FILE_PATH_ROOM ) )
			return false;

		return CreateDirectoryW ( wide, nullptr ) || GetLastError ( ) == ERROR_ALREADY_EXISTS;

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

	bool write_whole ( const char* path, const char* data, size_t size ) {

		file_handle file;

		if ( !open_file ( file, path, true ) )
			return false;

		bool fine = truncate_file ( file, 0 ) && ( !size || write_at ( file, 0, data, size ) ) && sync_file ( file );

		close_file ( file );

		return fine;

	}

	bool environment_path ( const wchar_t* name, text& out ) {

		wchar_t wide [ FILE_PATH_ROOM ];
		DWORD size = GetEnvironmentVariableW ( name, wide, FILE_PATH_ROOM );
		char narrow [ FILE_PATH_ROOM ];

		out.clear ( );

		if ( !size || size >= FILE_PATH_ROOM || WideCharToMultiByte ( CP_UTF8, 0, wide, -1, narrow, sizeof ( narrow ), nullptr, nullptr ) <= 0 )
			return false;

		out.add ( narrow );

		return true;

	}

}