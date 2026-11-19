#include "base/runtime/memory.hh"

#include <windows.h>

extern "C" {

	int _fltused = 0;

	typedef uint64_t word_unaligned __attribute__ ( ( aligned ( 1 ), may_alias ) );
	typedef uint32_t half_unaligned __attribute__ ( ( aligned ( 1 ), may_alias ) );
	typedef uint16_t pair_unaligned __attribute__ ( ( aligned ( 1 ), may_alias ) );

	__attribute__ ( ( no_builtin ( "memset" ) ) ) void* memset ( void* target, int value, size_t size ) {

		uint8_t* to = static_cast < uint8_t* > ( target );

		if ( size <= 64 ) {

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

		if ( size <= 64 ) {

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

	void* memmove ( void* target, const void* source, size_t size ) {

		uint8_t* to = static_cast < uint8_t* > ( target );
		const uint8_t* from = static_cast < const uint8_t* > ( source );

		if ( to <= from || to >= from + size )
			return memcpy ( target, source, size );

		for ( size_t i = size; i > 0; i-- )
			to [ i - 1 ] = from [ i - 1 ];

		return target;

	}

	int memcmp ( const void* a, const void* b, size_t size ) {

		const uint8_t* left = static_cast < const uint8_t* > ( a );
		const uint8_t* right = static_cast < const uint8_t* > ( b );

		for ( size_t i = 0; i < size; i++ )
			if ( left [ i ] != right [ i ] )
				return left [ i ] < right [ i ] ? -1 : 1;

		return 0;

	}

}

namespace base {

	void* allocate ( size_t size ) {

		return HeapAlloc ( GetProcessHeap ( ), HEAP_ZERO_MEMORY, size ? size : 1 );

	}

	void release ( void* block ) {

		if ( block )
			HeapFree ( GetProcessHeap ( ), 0, block );

	}

	void fill ( void* target, uint8_t value, size_t size ) {

		memset ( target, value, size );

	}

	void copy ( void* target, const void* source, size_t size ) {

		memcpy ( target, source, size );

	}

	void move ( void* target, const void* source, size_t size ) {

		memmove ( target, source, size );

	}

	bool same_bytes ( const void* a, const void* b, size_t size ) {

		return memcmp ( a, b, size ) == 0;

	}

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
