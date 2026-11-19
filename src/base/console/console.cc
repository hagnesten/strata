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

	static char input_buffer [ 4096 ];
	static uint32_t input_used;
	static uint32_t input_at;
	static bool input_done;

	bool read_line ( text& out ) {

		out.clear ( );

		for ( ;; ) {

			if ( input_at == input_used ) {

				if ( input_done )
					return out.size > 0;

				HANDLE handle = GetStdHandle ( STD_INPUT_HANDLE );
				DWORD read = 0;

				if ( !handle || handle == INVALID_HANDLE_VALUE || !ReadFile ( handle, input_buffer, sizeof ( input_buffer ), &read, nullptr ) || !read ) {

					input_done = true;

					return out.size > 0;

				}

				input_used = read;
				input_at = 0;

			}

			char here = input_buffer [ input_at++ ];

			if ( here == '\n' )
				return true;

			if ( here != '\r' )
				out.add ( here );

		}

	}

	void use_utf8 ( ) {

		SetConsoleOutputCP ( CP_UTF8 );
		SetConsoleCP ( CP_UTF8 );

	}

}
