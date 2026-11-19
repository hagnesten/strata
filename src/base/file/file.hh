#pragma once

#include "base/text/text.hh"

#define FILE_PATH_ROOM 520

namespace base {

	struct file_handle {

		void* handle;

	};

	struct file_faults {

		bool armed;
		bool tripped;
		bool torn;
		uint64_t countdown;
		uint64_t operations;

	};

	extern file_faults faults;

	void plan_fault ( uint64_t after, bool torn );

	void clear_faults ( );

	bool open_file ( file_handle& out, const char* path, bool create );

	bool read_at ( const file_handle& file, uint64_t offset, void* out, size_t size );

	bool write_at ( const file_handle& file, uint64_t offset, const void* data, size_t size );

	bool sync_file ( const file_handle& file );

	uint64_t file_size ( const file_handle& file );

	bool truncate_file ( const file_handle& file, uint64_t size );

	void close_file ( file_handle& file );

	bool erase_path ( const char* path );

	bool path_exists ( const char* path );

	bool wide_path ( const char* path, wchar_t* out, size_t room );

	bool temp_path ( char* out, size_t room, const char* name );

}
