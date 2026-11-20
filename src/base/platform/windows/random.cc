#include "base/platform/os.hh"

#include <windows.h>
#include <bcrypt.h>

namespace base {

	namespace os {

		bool random_bytes ( void* out, size_t size ) {

			uint8_t* at = static_cast < uint8_t* > ( out );

			while ( size ) {

				ULONG chunk = size > 0x10000000 ? 0x10000000 : static_cast < ULONG > ( size );

				if ( BCryptGenRandom ( nullptr, at, chunk, BCRYPT_USE_SYSTEM_PREFERRED_RNG ) != 0 )
					return false;

				at += chunk;
				size -= chunk;

			}

			return true;

		}

	}

}
