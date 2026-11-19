#pragma once

#include "base/file/file.hh"

#define FOLDER_NAME_ROOM 260

namespace base {

	struct folder_entry {

		char name [ FOLDER_NAME_ROOM ];
		bool directory;
		uint64_t size;

	};

	struct folder_list {

		folder_entry* items;
		uint32_t count;
		uint32_t room;

	};

	bool list_folder ( const char* path, folder_list& out );

	bool list_drives ( folder_list& out );

	void forget_list ( folder_list& list );

	bool make_folder ( const char* path );

	bool read_whole ( const char* path, text& out );

	bool write_whole ( const char* path, const char* data, size_t size );

	bool environment_path ( const wchar_t* name, text& out );

}