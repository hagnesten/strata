#include "base/console/console.hh"

#include <windows.h>

namespace base {

	size_t length ( const char* text ) {

		size_t size = 0;

		while ( text && text [ size ] )
			size++;

		return size;

	}

	static void write_to ( DWORD which, const char* text ) {

		HANDLE handle = GetStdHandle ( which );
		DWORD written = 0;

		if ( handle && handle != INVALID_HANDLE_VALUE )
			WriteFile ( handle, text, static_cast < DWORD > ( length ( text ) ), &written, nullptr );

	}

	void write ( const char* text ) {

		write_to ( STD_OUTPUT_HANDLE, text );

	}

	void write_error ( const char* text ) {

		write_to ( STD_ERROR_HANDLE, text );

	}

}
