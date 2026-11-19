#include "base/arena/arena.hh"

namespace base {

	arena::~arena ( ) {

		reset ( );

	}

	void* arena::take ( size_t size ) {

		size = ( size + 15 ) & ~static_cast < size_t > ( 15 );

		if ( !first || first->size - first->used < size ) {

			size_t room = max < size_t > ( ARENA_BLOCK, size + sizeof ( arena_block ) + 16 );
			arena_block* block = static_cast < arena_block* > ( allocate ( room ) );

			if ( !block )
				return nullptr;

			block->next = first;
			block->size = room;
			block->used = ( sizeof ( arena_block ) + 15 ) & ~static_cast < size_t > ( 15 );
			first = block;
			total += room;

		}

		void* at = reinterpret_cast < uint8_t* > ( first ) + first->used;

		first->used += size;
		fill ( at, 0, size );

		return at;

	}

	char* arena::keep ( const char* data, size_t size ) {

		char* out = static_cast < char* > ( take ( size + 1 ) );

		if ( !out )
			return nullptr;

		if ( size )
			copy ( out, data, size );

		out [ size ] = 0;

		return out;

	}

	void arena::reset ( ) {

		while ( first ) {

			arena_block* next = first->next;

			release ( first );
			first = next;

		}

		total = 0;

	}

}
