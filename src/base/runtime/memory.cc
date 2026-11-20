#include "base/runtime/memory.hh"
#include "base/platform/os.hh"

extern "C" {

#if STRATA_WINDOWS
	int _fltused = 0;
#endif

	typedef uint64_t word_unaligned __attribute__ ( ( aligned ( 1 ), may_alias ) );
	typedef uint32_t half_unaligned __attribute__ ( ( aligned ( 1 ), may_alias ) );
	typedef uint16_t pair_unaligned __attribute__ ( ( aligned ( 1 ), may_alias ) );

	__attribute__ ( ( no_builtin ( "memset" ) ) ) void* memset ( void* target, int value, size_t size ) {

		uint8_t* to = static_cast < uint8_t* > ( target );

		if ( size <= 512 ) {

			uint64_t pattern = static_cast < uint8_t > ( value ) * 0x0101010101010101ull;

			if ( size >= 8 ) {

				for ( size_t i = 0; i + 8 < size; i += 8 )
					*reinterpret_cast < word_unaligned* > ( to + i ) = pattern;

				*reinterpret_cast < word_unaligned* > ( to + size - 8 ) = pattern;

			} else if ( size >= 4 ) {

				*reinterpret_cast < half_unaligned* > ( to ) = static_cast < uint32_t > ( pattern );
				*reinterpret_cast < half_unaligned* > ( to + size - 4 ) = static_cast < uint32_t > ( pattern );

			} else {

				for ( size_t i = 0; i < size; i++ )
					to [ i ] = static_cast < uint8_t > ( value );

			}

			return target;

		}

		void* at = target;

		__asm__ volatile ( "rep stosb" : "+D" ( at ), "+c" ( size ) : "a" ( value ) : "memory" );

		return target;

	}

	__attribute__ ( ( no_builtin ( "memcpy" ) ) ) void* memcpy ( void* target, const void* source, size_t size ) {

		uint8_t* to = static_cast < uint8_t* > ( target );
		const uint8_t* from = static_cast < const uint8_t* > ( source );

		if ( size <= 512 ) {

			if ( size >= 8 ) {

				uint64_t last = *reinterpret_cast < const word_unaligned* > ( from + size - 8 );

				for ( size_t i = 0; i + 8 < size; i += 8 )
					*reinterpret_cast < word_unaligned* > ( to + i ) = *reinterpret_cast < const word_unaligned* > ( from + i );

				*reinterpret_cast < word_unaligned* > ( to + size - 8 ) = last;

			} else if ( size >= 4 ) {

				uint32_t first = *reinterpret_cast < const half_unaligned* > ( from );
				uint32_t last = *reinterpret_cast < const half_unaligned* > ( from + size - 4 );

				*reinterpret_cast < half_unaligned* > ( to ) = first;
				*reinterpret_cast < half_unaligned* > ( to + size - 4 ) = last;

			} else if ( size >= 2 ) {

				uint16_t first = *reinterpret_cast < const pair_unaligned* > ( from );
				uint16_t last = *reinterpret_cast < const pair_unaligned* > ( from + size - 2 );

				*reinterpret_cast < pair_unaligned* > ( to ) = first;
				*reinterpret_cast < pair_unaligned* > ( to + size - 2 ) = last;

			} else if ( size )
				to [ 0 ] = from [ 0 ];

			return target;

		}

		void* at = target;

		__asm__ volatile ( "rep movsb" : "+D" ( at ), "+S" ( source ), "+c" ( size ) : : "memory" );

		return target;

	}

	__attribute__ ( ( no_builtin ( "memmove" ) ) ) void* memmove ( void* target, const void* source, size_t size ) {

		uint8_t* to = static_cast < uint8_t* > ( target );
		const uint8_t* from = static_cast < const uint8_t* > ( source );

		if ( to <= from || to >= from + size )
			return memcpy ( target, source, size );

		for ( size_t i = size; i > 0; i-- )
			to [ i - 1 ] = from [ i - 1 ];

		return target;

	}

	__attribute__ ( ( no_builtin ( "memcmp", "bcmp" ) ) ) int memcmp ( const void* a, const void* b, size_t size ) {

		const uint8_t* left = static_cast < const uint8_t* > ( a );
		const uint8_t* right = static_cast < const uint8_t* > ( b );

		while ( size >= 8 ) {

			uint64_t x = *reinterpret_cast < const word_unaligned* > ( left );
			uint64_t y = *reinterpret_cast < const word_unaligned* > ( right );

			if ( x != y ) {

				x = __builtin_bswap64 ( x );
				y = __builtin_bswap64 ( y );

				return x < y ? -1 : 1;

			}

			left += 8;
			right += 8;
			size -= 8;

		}

		for ( size_t i = 0; i < size; i++ )
			if ( left [ i ] != right [ i ] )
				return left [ i ] < right [ i ] ? -1 : 1;

		return 0;

	}

	__attribute__ ( ( no_builtin ( "memcmp", "bcmp" ) ) ) int bcmp ( const void* a, const void* b, size_t size ) {

		return memcmp ( a, b, size );

	}

}

namespace base {

	void swap_bytes ( void* a, void* b, size_t size ) {

		uint8_t* left = static_cast < uint8_t* > ( a );
		uint8_t* right = static_cast < uint8_t* > ( b );

		for ( size_t i = 0; i < size; i++ ) {

			uint8_t held = left [ i ];

			left [ i ] = right [ i ];
			right [ i ] = held;

		}

	}

}
