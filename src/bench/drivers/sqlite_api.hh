#pragma once

#include <stdint.h>

#define SQLITE_OK 0
#define SQLITE_ROW 100
#define SQLITE_DONE 101
#define SQLITE_INTEGER 1
#define SQLITE_FLOAT 2
#define SQLITE_TEXT 3
#define SQLITE_BLOB 4
#define SQLITE_NULL 5
#define SQLITE_OPEN_READWRITE 0x00000002
#define SQLITE_OPEN_CREATE 0x00000004
#define SQLITE_OPEN_NOMUTEX 0x00008000

namespace bench {

	struct sqlite_api {

		void* module;
		int ( *initialize ) ( );
		int ( *open_v2 ) ( const char* path, void** db, int flags, const char* vfs );
		int ( *close_v2 ) ( void* db );
		int ( *exec ) ( void* db, const char* sql, void* callback, void* context, char** problem );
		int ( *prepare_v2 ) ( void* db, const char* sql, int size, void** statement, const char** tail );
		int ( *bind_int64 ) ( void* statement, int index, int64_t value );
		int ( *bind_double ) ( void* statement, int index, double value );
		int ( *bind_text ) ( void* statement, int index, const char* value, int size, void* destructor );
		int ( *bind_null ) ( void* statement, int index );
		int ( *step ) ( void* statement );
		int ( *reset ) ( void* statement );
		int ( *finalize ) ( void* statement );
		int ( *column_count ) ( void* statement );
		int ( *column_type ) ( void* statement, int column );
		int64_t ( *column_int64 ) ( void* statement, int column );
		double ( *column_double ) ( void* statement, int column );
		const unsigned char* ( *column_text ) ( void* statement, int column );
		int ( *column_bytes ) ( void* statement, int column );
		const char* ( *errmsg ) ( void* db );
		const char* ( *libversion ) ( );
		void ( *free ) ( void* block );

	};

	bool load_sqlite ( sqlite_api& out, const char* path );

	void unload_sqlite ( sqlite_api& item );

}
