#include "base/platform/linux/heap.hh"
#include "base/platform/os.hh"
#include "base/runtime/memory.hh"

namespace base {

	static heap_state heap;

	static void take_lock ( ) {

		while ( __atomic_exchange_n ( &heap.lock, 1u, __ATOMIC_ACQUIRE ) )
			while ( __atomic_load_n ( &heap.lock, __ATOMIC_RELAXED ) )
				__builtin_ia32_pause ( );

	}

	static void drop_lock ( ) {

		__atomic_store_n ( &heap.lock, 0u, __ATOMIC_RELEASE );

	}

	static uint32_t class_of ( size_t total ) {

		if ( total <= 32 )
			return 0;

		uint32_t power = 63 - static_cast < uint32_t > ( __builtin_clzll ( total - 1 ) );
		size_t base_size = static_cast < size_t > ( 1 ) << power;
		size_t quarter = base_size / 4;
		size_t step = ( total - base_size + quarter - 1 ) / quarter;

		return ( power - 5 ) * 4 + static_cast < uint32_t > ( step - 1 ) + 1;

	}

	static size_t class_size ( uint32_t index ) {

		if ( !index )
			return 32;

		uint32_t power = 5 + ( index - 1 ) / 4;
		size_t base_size = static_cast < size_t > ( 1 ) << power;

		return base_size + ( ( index - 1 ) % 4 + 1 ) * ( base_size / 4 );

	}

	static uint8_t* carve ( size_t size ) {

		if ( heap.chunk_left < size ) {

			size_t room = size > HEAP_CHUNK / 4 ? size : HEAP_CHUNK;
			uint8_t* made = static_cast < uint8_t* > ( os::allocate_pages ( room ) );

			if ( !made )
				return nullptr;

			if ( room != HEAP_CHUNK )
				return made;

			heap.chunk = made;
			heap.chunk_left = room;

		}

		uint8_t* at = heap.chunk;

		heap.chunk += size;
		heap.chunk_left -= size;

		return at;

	}

	void* allocate ( size_t size ) {

		size_t total = ( size ? size : 1 ) + HEAP_HEADER;

		if ( total > HEAP_LARGEST ) {

			size_t mapped = ( total + 4095 ) & ~static_cast < size_t > ( 4095 );
			heap_block* block = static_cast < heap_block* > ( os::allocate_pages ( mapped ) );

			if ( !block )
				return nullptr;

			block->size = mapped | HEAP_LARGE_FLAG;
			block->magic = HEAP_MAGIC;

			return block + 1;

		}

		uint32_t index = class_of ( total );
		size_t room = class_size ( index );
		uint8_t* at = nullptr;

		take_lock ( );

		if ( heap.free [ index ] ) {

			at = reinterpret_cast < uint8_t* > ( heap.free [ index ] );
			heap.free [ index ] = heap.free [ index ]->next;

		} else
			at = carve ( room );

		drop_lock ( );

		if ( !at )
			return nullptr;

		heap_block* block = reinterpret_cast < heap_block* > ( at );

		block->size = index;
		block->magic = HEAP_MAGIC;
		fill ( block + 1, 0, size );

		return block + 1;

	}

	void release ( void* pointer ) {

		if ( !pointer )
			return;

		heap_block* block = static_cast < heap_block* > ( pointer ) - 1;

		if ( block->magic != HEAP_MAGIC )
			return;

		if ( block->size & HEAP_LARGE_FLAG ) {

			os::release_pages ( block, static_cast < size_t > ( block->size & ~HEAP_LARGE_FLAG ) );

			return;

		}

		uint32_t index = static_cast < uint32_t > ( block->size );

		if ( index >= HEAP_CLASSES )
			return;

		heap_free* entry = reinterpret_cast < heap_free* > ( block );

		block->magic = 0;
		take_lock ( );
		entry->next = heap.free [ index ];
		heap.free [ index ] = entry;
		drop_lock ( );

	}

}
