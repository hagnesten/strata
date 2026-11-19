#include "bench/drivers/sqlite_api.hh"
#include "base/file/file.hh"

#include <windows.h>

namespace bench {

	template < typename type > static bool find ( HMODULE module, const char* name, type& out ) {

		out = reinterpret_cast < type > ( reinterpret_cast < void* > ( GetProcAddress ( module, name ) ) );

		return out != nullptr;

	}

	bool load_sqlite ( sqlite_api& out, const char* path ) {

		wchar_t wide [ FILE_PATH_ROOM ];

		out = { };

		if ( !base::wide_path ( path, wide, FILE_PATH_ROOM ) )
			return false;

		HMODULE module = LoadLibraryExW ( wide, nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS );

		if ( !module )
			module = LoadLibraryW ( wide );

		if ( !module )
			return false;

		out.module = module;

		bool fine = find ( module, "sqlite3_initialize", out.initialize ) && find ( module, "sqlite3_open_v2", out.open_v2 ) && find ( module, "sqlite3_close_v2", out.close_v2 ) && find ( module, "sqlite3_exec", out.exec ) && find ( module, "sqlite3_prepare_v2", out.prepare_v2 );

		fine = fine && find ( module, "sqlite3_bind_int64", out.bind_int64 ) && find ( module, "sqlite3_bind_double", out.bind_double ) && find ( module, "sqlite3_bind_text", out.bind_text ) && find ( module, "sqlite3_bind_null", out.bind_null );
		fine = fine && find ( module, "sqlite3_step", out.step ) && find ( module, "sqlite3_reset", out.reset ) && find ( module, "sqlite3_finalize", out.finalize ) && find ( module, "sqlite3_column_count", out.column_count );
		fine = fine && find ( module, "sqlite3_column_type", out.column_type ) && find ( module, "sqlite3_column_int64", out.column_int64 ) && find ( module, "sqlite3_column_double", out.column_double );
		fine = fine && find ( module, "sqlite3_column_text", out.column_text ) && find ( module, "sqlite3_column_bytes", out.column_bytes ) && find ( module, "sqlite3_errmsg", out.errmsg );
		fine = fine && find ( module, "sqlite3_libversion", out.libversion ) && find ( module, "sqlite3_free", out.free );

		if ( fine && out.initialize ( ) != SQLITE_OK )
			fine = false;

		if ( !fine )
			unload_sqlite ( out );

		return fine;

	}

	void unload_sqlite ( sqlite_api& item ) {

		if ( item.module )
			FreeLibrary ( static_cast < HMODULE > ( item.module ) );

		item = { };

	}

}
