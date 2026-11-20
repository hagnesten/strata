#pragma once

#include <stdint.h>
#include <stddef.h>

namespace base {

	void* allocate ( size_t size );

	void release ( void* block );

	inline void fill ( void* target, uint8_t value, size_t size ) {

		__builtin_memset ( target, value, size );

	}

	inline void copy ( void* target, const void* source, size_t size ) {

		__builtin_memcpy ( target, source, size );

	}

	inline void move ( void* target, const void* source, size_t size ) {

		__builtin_memmove ( target, source, size );

	}

	inline bool same_bytes ( const void* a, const void* b, size_t size ) {

		return __builtin_memcmp ( a, b, size ) == 0;

	}

	void swap_bytes ( void* a, void* b, size_t size );

	template < typename type > type* make ( size_t count ) {

		return static_cast < type* > ( allocate ( sizeof ( type ) * count ) );

	}

	template < typename type > type min ( type a, type b ) {

		return a < b ? a : b;

	}

	template < typename type > type max ( type a, type b ) {

		return a > b ? a : b;

	}

}
