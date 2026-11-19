#include "base/console/console.hh"
#include "base/platform/os.hh"

namespace base {

	void write ( const char* value ) {

		os::write_stream ( OS_OUT, value, length ( value ) );

	}

	void write ( const text& value ) {

		os::write_stream ( OS_OUT, value.value ( ), value.size );

	}

	void write_error ( const char* value ) {

		os::write_stream ( OS_ERROR, value, length ( value ) );

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

				size_t got = 0;

				if ( !os::read_input ( input_buffer, sizeof ( input_buffer ), got ) || !got ) {

					input_done = true;

					return out.size > 0;

				}

				input_used = static_cast < uint32_t > ( got );
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

		os::use_utf8 ( );

	}

}
