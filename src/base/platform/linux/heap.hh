#pragma once

#include <stdint.h>
#include <stddef.h>

#define HEAP_CLASSES 53
#define HEAP_HEADER 16
#define HEAP_LARGEST ( 256u * 1024u )
#define HEAP_CHUNK ( 1u << 20 )
#define HEAP_LARGE_FLAG 0x8000000000000000ull
#define HEAP_MAGIC 0x5354524154414845ull

namespace base {

	struct heap_block {

		uint64_t size;
		uint64_t magic;

	};

	struct heap_free {

		heap_free* next;

	};

	struct heap_state {

		volatile uint32_t lock;
		heap_free* free [ HEAP_CLASSES ];
		uint8_t* chunk;
		size_t chunk_left;

	};

}
