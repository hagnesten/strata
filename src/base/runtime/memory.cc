#include "base/runtime/memory.hh"

#include <windows.h>

extern "C" {

	int _fltused = 0;

	void* memset ( void* target, int value, size_t size ) {

		void* at = target;

		__asm__ volatile ( "rep stosb" : "+D" ( at ), "+c" ( size ) : "a" ( value ) : "memory" );

		return target;

	}

	void* memcpy ( void* target, const void* source, size_t size ) {

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
