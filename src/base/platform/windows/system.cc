#include "base/platform/os.hh"
#include "base/platform/windows/wide.hh"
#include "base/text/text.hh"
#include "base/file/file.hh"

#include <windows.h>

namespace base {

	namespace os {

		bool wide_path ( const char* path, wchar_t* out, size_t room ) {

			return MultiByteToWideChar ( CP_UTF8, 0, path, -1, out, static_cast < int32_t > ( room ) ) > 0;

		}

		static bool narrow_add ( text& out, const wchar_t* wide ) {

			char narrow [ FILE_PATH_ROOM * 3 ];

			if ( WideCharToMultiByte ( CP_UTF8, 0, wide, -1, narrow, sizeof ( narrow ), nullptr, nullptr ) <= 0 )
				return false;

			out.add ( narrow );

			return true;

		}

		void* allocate_pages ( size_t size ) {

			return VirtualAlloc ( nullptr, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE );

		}

		void release_pages ( void* at, size_t size ) {

			( void ) size;

			if ( at )
				VirtualFree ( at, 0, MEM_RELEASE );

		}

		bool open_file ( void*& out, const char* path, bool write, bool create, bool through ) {

			wchar_t wide [ FILE_PATH_ROOM ];

			out = nullptr;

			if ( !wide_path ( path, wide, FILE_PATH_ROOM ) )
				return false;

			HANDLE handle = write ? CreateFileW ( wide, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, create ? OPEN_ALWAYS : OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_RANDOM_ACCESS | ( through ? FILE_FLAG_WRITE_THROUGH : 0 ), nullptr ) : CreateFileW ( wide, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr );

			if ( handle == INVALID_HANDLE_VALUE )
				return false;

			out = handle;

			return true;

		}

		static OVERLAPPED at_offset ( uint64_t offset ) {

			OVERLAPPED place = { };

			place.Offset = static_cast < DWORD > ( offset );
			place.OffsetHigh = static_cast < DWORD > ( offset >> 32 );

			return place;

		}

		bool read_at ( void* file, uint64_t offset, void* out, size_t size, size_t& got ) {

			OVERLAPPED place = at_offset ( offset );
			DWORD read = 0;
			bool fine = ReadFile ( file, out, static_cast < DWORD > ( size ), &read, &place ) != 0;

			got = read;

			return fine;

		}

		bool write_at ( void* file, uint64_t offset, const void* data, size_t size, size_t& put ) {

			OVERLAPPED place = at_offset ( offset );
			DWORD wrote = 0;
			bool fine = WriteFile ( file, data, static_cast < DWORD > ( size ), &wrote, &place ) != 0;

			put = wrote;

			return fine;

		}

		bool sync_file ( void* file ) {

			return FlushFileBuffers ( file ) != 0;

		}

		bool file_size ( void* file, uint64_t& out ) {

			LARGE_INTEGER size = { };

			if ( !GetFileSizeEx ( file, &size ) )
				return false;

			out = static_cast < uint64_t > ( size.QuadPart );

			return true;

		}

		bool truncate_file ( void* file, uint64_t size ) {

			FILE_END_OF_FILE_INFO end = { };

			end.EndOfFile.QuadPart = static_cast < LONGLONG > ( size );

			return SetFileInformationByHandle ( file, FileEndOfFileInfo, &end, sizeof ( end ) ) != 0;

		}

		void close_file ( void* file ) {

			CloseHandle ( file );

		}

		void* map_file ( void* file, uint64_t offset, size_t size, void*& mapping ) {

			uint64_t end = offset + size;

			mapping = CreateFileMappingW ( file, nullptr, PAGE_READWRITE, static_cast < DWORD > ( end >> 32 ), static_cast < DWORD > ( end ), nullptr );

			if ( !mapping )
				return nullptr;

			void* view = MapViewOfFile ( mapping, FILE_MAP_READ | FILE_MAP_WRITE, static_cast < DWORD > ( offset >> 32 ), static_cast < DWORD > ( offset ), size );

			if ( !view ) {

				CloseHandle ( mapping );
				mapping = nullptr;

			}

			return view;

		}

		void unmap_file ( void* view, size_t size, void* mapping ) {

			( void ) size;

			if ( view )
				UnmapViewOfFile ( view );

			if ( mapping )
				CloseHandle ( mapping );

		}

		bool lock_range ( void* file, uint64_t offset, uint64_t size, bool exclusive, bool wait ) {

			OVERLAPPED place = at_offset ( offset );
			DWORD flags = ( exclusive ? LOCKFILE_EXCLUSIVE_LOCK : 0 ) | ( wait ? 0 : LOCKFILE_FAIL_IMMEDIATELY );

			return LockFileEx ( file, flags, 0, static_cast < DWORD > ( size ), static_cast < DWORD > ( size >> 32 ), &place ) != 0;

		}

		void unlock_range ( void* file, uint64_t offset, uint64_t size ) {

			OVERLAPPED place = at_offset ( offset );

			UnlockFileEx ( file, 0, static_cast < DWORD > ( size ), static_cast < DWORD > ( size >> 32 ), &place );

		}

		void sleep_milliseconds ( uint32_t milliseconds ) {

			Sleep ( milliseconds );

		}

		bool erase_path ( const char* path ) {

			wchar_t wide [ FILE_PATH_ROOM ];

			return wide_path ( path, wide, FILE_PATH_ROOM ) && DeleteFileW ( wide );

		}

		bool replace_path ( const char* from, const char* to ) {

			wchar_t wide_from [ FILE_PATH_ROOM ];
			wchar_t wide_to [ FILE_PATH_ROOM ];

			return wide_path ( from, wide_from, FILE_PATH_ROOM ) && wide_path ( to, wide_to, FILE_PATH_ROOM ) && MoveFileExW ( wide_from, wide_to, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH );

		}

		bool path_exists ( const char* path ) {

			wchar_t wide [ FILE_PATH_ROOM ];

			return wide_path ( path, wide, FILE_PATH_ROOM ) && GetFileAttributesW ( wide ) != INVALID_FILE_ATTRIBUTES;

		}

		bool make_folder ( const char* path ) {

			wchar_t wide [ FILE_PATH_ROOM ];

			if ( !wide_path ( path, wide, FILE_PATH_ROOM ) )
				return false;

			return CreateDirectoryW ( wide, nullptr ) || GetLastError ( ) == ERROR_ALREADY_EXISTS;

		}

		bool temp_folder ( text& out ) {

			wchar_t folder [ FILE_PATH_ROOM ];
			DWORD got = GetTempPathW ( FILE_PATH_ROOM, folder );

			out.clear ( );

			return got && got < FILE_PATH_ROOM && narrow_add ( out, folder );

		}

		bool environment ( const char* name, text& out ) {

			wchar_t wide_name [ 128 ];
			wchar_t wide [ FILE_PATH_ROOM ];

			out.clear ( );

			if ( !wide_path ( name, wide_name, 128 ) )
				return false;

			DWORD size = GetEnvironmentVariableW ( wide_name, wide, FILE_PATH_ROOM );

			return size && size < FILE_PATH_ROOM && narrow_add ( out, wide );

		}

		bool list_folder ( const char* path, folder_visit visit, void* context ) {

			text pattern;
			wchar_t wide [ FILE_PATH_ROOM ];

			pattern.add ( path );

			if ( pattern.size && pattern.data [ pattern.size - 1 ] != '\\' && pattern.data [ pattern.size - 1 ] != '/' )
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

				char name [ FILE_PATH_ROOM ];

				if ( WideCharToMultiByte ( CP_UTF8, 0, found.cFileName, -1, name, sizeof ( name ), nullptr, nullptr ) <= 0 )
					continue;

				fine = visit ( context, name, ( found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY ) != 0, static_cast < uint64_t > ( found.nFileSizeHigh ) << 32 | found.nFileSizeLow );

			} while ( fine && FindNextFileW ( search, &found ) );

			FindClose ( search );

			return fine;

		}

		bool list_roots ( folder_visit visit, void* context ) {

			DWORD drives = GetLogicalDrives ( );

			for ( uint32_t d = 0; d < 26; d++ ) {

				char name [ 3 ] = { static_cast < char > ( 'A' + d ), ':', 0 };

				if ( ( drives & ( 1u << d ) ) && !visit ( context, name, true, 0 ) )
					return false;

			}

			return true;

		}

		char separator ( ) {

			return '\\';

		}

		void write_stream ( int32_t which, const char* data, size_t size ) {

			HANDLE handle = GetStdHandle ( which == OS_ERROR ? STD_ERROR_HANDLE : STD_OUTPUT_HANDLE );
			DWORD written = 0;

			if ( handle && handle != INVALID_HANDLE_VALUE && size )
				WriteFile ( handle, data, static_cast < DWORD > ( size ), &written, nullptr );

		}

		bool read_input ( char* out, size_t room, size_t& got ) {

			HANDLE handle = GetStdHandle ( STD_INPUT_HANDLE );
			DWORD read = 0;

			got = 0;

			if ( !handle || handle == INVALID_HANDLE_VALUE || !ReadFile ( handle, out, static_cast < DWORD > ( room ), &read, nullptr ) )
				return false;

			got = read;

			return true;

		}

		void use_utf8 ( ) {

			SetConsoleOutputCP ( CP_UTF8 );
			SetConsoleCP ( CP_UTF8 );

		}

		int64_t unix_milliseconds ( ) {

			FILETIME now;

			GetSystemTimeAsFileTime ( &now );

			uint64_t ticks = static_cast < uint64_t > ( now.dwHighDateTime ) << 32 | now.dwLowDateTime;

			return static_cast < int64_t > ( ticks / 10000 ) - 11644473600000ll;

		}

		int64_t local_offset_milliseconds ( ) {

			TIME_ZONE_INFORMATION zone;
			DWORD mode = GetTimeZoneInformation ( &zone );
			int64_t bias = zone.Bias;

			if ( mode == TIME_ZONE_ID_DAYLIGHT )
				bias += zone.DaylightBias;
			else if ( mode == TIME_ZONE_ID_STANDARD )
				bias += zone.StandardBias;

			return -bias * 60000;

		}

		uint64_t ticks ( ) {

			LARGE_INTEGER now;

			QueryPerformanceCounter ( &now );

			return static_cast < uint64_t > ( now.QuadPart );

		}

		uint64_t tick_rate ( ) {

			LARGE_INTEGER rate;

			QueryPerformanceFrequency ( &rate );

			return static_cast < uint64_t > ( rate.QuadPart );

		}

		uint32_t process_id ( ) {

			return GetCurrentProcessId ( );

		}

		void exit_process ( int32_t code ) {

			ExitProcess ( static_cast < UINT > ( code ) );

		}

		bool own_program ( text& out ) {

			wchar_t wide [ 4096 ];
			DWORD length = GetModuleFileNameW ( nullptr, wide, 4096 );
			char narrow [ 4096 * 3 ];

			if ( !length || length >= 4096 )
				return false;

			int written = WideCharToMultiByte ( CP_UTF8, 0, wide, static_cast < int > ( length ), narrow, sizeof ( narrow ), nullptr, nullptr );

			if ( written <= 0 )
				return false;

			out.clear ( );
			out.add ( narrow, static_cast < size_t > ( written ) );

			return true;

		}

		static void quote_argument ( text& line, const char* argument ) {

			line.add ( '"' );

			uint32_t slashes = 0;

			for ( const char* at = argument; *at; at++ ) {

				if ( *at == '\\' ) {

					slashes++;

					continue;

				}

				for ( uint32_t i = 0; i < ( *at == '"' ? slashes * 2 + 1 : slashes ); i++ )
					line.add ( '\\' );

				slashes = 0;
				line.add ( *at );

			}

			for ( uint32_t i = 0; i < slashes * 2; i++ )
				line.add ( '\\' );

			line.add ( '"' );

		}

		bool start_process ( const char* program, const char* const* arguments, uint32_t count, void*& out ) {

			text line;

			out = nullptr;
			quote_argument ( line, program );

			for ( uint32_t i = 0; i < count; i++ ) {

				line.add ( ' ' );
				quote_argument ( line, arguments [ i ] );

			}

			int units = MultiByteToWideChar ( CP_UTF8, 0, line.value ( ), static_cast < int > ( line.size ), nullptr, 0 );

			if ( units <= 0 || units >= 32767 )
				return false;

			wchar_t* wide = static_cast < wchar_t* > ( HeapAlloc ( GetProcessHeap ( ), HEAP_ZERO_MEMORY, ( static_cast < size_t > ( units ) + 1 ) * sizeof ( wchar_t ) ) );

			if ( !wide )
				return false;

			MultiByteToWideChar ( CP_UTF8, 0, line.value ( ), static_cast < int > ( line.size ), wide, units );

			STARTUPINFOW start = { };
			PROCESS_INFORMATION made = { };

			start.cb = sizeof ( start );

			BOOL fine = CreateProcessW ( nullptr, wide, nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &start, &made );

			HeapFree ( GetProcessHeap ( ), 0, wide );

			if ( !fine )
				return false;

			CloseHandle ( made.hThread );
			out = made.hProcess;

			return true;

		}

		bool process_finished ( void* process, int32_t& code ) {

			DWORD status = 0;

			if ( !process || WaitForSingleObject ( process, 0 ) != WAIT_OBJECT_0 || !GetExitCodeProcess ( process, &status ) )
				return false;

			code = static_cast < int32_t > ( status );
			CloseHandle ( process );

			return true;

		}

	}

}
