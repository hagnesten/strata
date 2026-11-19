#pragma once

#include <stdint.h>
#include <stddef.h>

#if defined ( _WIN32 )
#define STRATA_WINDOWS 1
#define STRATA_LINUX 0
#elif defined ( __linux__ )
#define STRATA_WINDOWS 0
#define STRATA_LINUX 1
#else
#error "Strata builds for Windows and Linux"
#endif

#define OS_OUT 1
#define OS_ERROR 2

namespace base {

	struct text;

	namespace os {

		typedef bool ( *folder_visit ) ( void* context, const char* name, bool directory, uint64_t size );

		void* allocate_pages ( size_t size );

		void release_pages ( void* at, size_t size );

		bool open_file ( void*& out, const char* path, bool write, bool create );

		bool read_at ( void* file, uint64_t offset, void* out, size_t size, size_t& got );

		bool write_at ( void* file, uint64_t offset, const void* data, size_t size, size_t& put );

		bool sync_file ( void* file );

		bool file_size ( void* file, uint64_t& out );

		bool truncate_file ( void* file, uint64_t size );

		void close_file ( void* file );

		void* map_file ( void* file, uint64_t offset, size_t size, void*& mapping );

		void unmap_file ( void* view, size_t size, void* mapping );

		bool lock_range ( void* file, uint64_t offset, uint64_t size, bool exclusive, bool wait );

		void unlock_range ( void* file, uint64_t offset, uint64_t size );

		void sleep_milliseconds ( uint32_t milliseconds );

		bool erase_path ( const char* path );

		bool replace_path ( const char* from, const char* to );

		bool path_exists ( const char* path );

		bool make_folder ( const char* path );

		bool temp_folder ( text& out );

		bool environment ( const char* name, text& out );

		bool list_folder ( const char* path, folder_visit visit, void* context );

		bool list_roots ( folder_visit visit, void* context );

		char separator ( );

		void write_stream ( int32_t which, const char* data, size_t size );

		bool read_input ( char* out, size_t room, size_t& got );

		void use_utf8 ( );

		int64_t unix_milliseconds ( );

		int64_t local_offset_milliseconds ( );

		uint64_t ticks ( );

		uint64_t tick_rate ( );

		uint32_t process_id ( );

		[[ noreturn ]] void exit_process ( int32_t code );

	}

}
