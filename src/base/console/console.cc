#include "base/console/console.hh"

#include <windows.h>

namespace base {

	static void write_to ( DWORD which, const char* value, size_t size ) {

		HANDLE handle = GetStdHandle ( which );
		DWORD written = 0;

		if ( handle && handle != INVALID_HANDLE_VALUE && size )
			WriteFile ( handle, value, static_cast < DWORD > ( size ), &written, nullptr );

	}

	void write ( const char* value ) {

		write_to ( STD_OUTPUT_HANDLE, value, length ( value ) );

	}

	void write ( const text& value ) {

		write_to ( STD_OUTPUT_HANDLE, value.value ( ), value.size );

	}

	void write_error ( const char* value ) {

		write_to ( STD_ERROR_HANDLE, value, length ( value ) );

	}

}
