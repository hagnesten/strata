#pragma once

#include <stdint.h>
#include <stddef.h>

#define SHARED_MAGIC 0x48535453u
#define SHARED_VERSION 1
#define SHARED_READERS 64
#define SHARED_IDLE 0xFFFFFFFFu
#define SHARED_SEGMENT_FRAMES 4096
#define SHARED_HASH_SLOTS 8192
#define SHARED_HEADER_BYTES 65536
#define SHARED_SEGMENT_BYTES ( SHARED_SEGMENT_FRAMES * 8 + SHARED_HASH_SLOTS * 4 )
#define SHARED_SEGMENTS 1024
#define SHARED_LOCKS 0x7FFF0000ull
#define SHARED_LOCK_PRESENT ( SHARED_LOCKS + 0 )
#define SHARED_LOCK_WRITE ( SHARED_LOCKS + 1 )
#define SHARED_LOCK_CHECKPOINT ( SHARED_LOCKS + 2 )
#define SHARED_LOCK_READER ( SHARED_LOCKS + 16 )
#define SHARED_BUSY_MILLISECONDS 5000

namespace storage {

	struct shared_state_header {

		uint32_t page_size;
		uint32_t page_count;
		uint32_t free_head;
		uint32_t free_count;
		uint32_t schema_root;
		uint32_t schema_version;
		uint64_t change;

	};

	struct shared_header {

		uint32_t magic;
		uint32_t version;
		volatile uint32_t sequence;
		volatile uint32_t generation;
		volatile uint32_t frames;
		volatile uint32_t backfilled;
		uint32_t salt [ 2 ];
		uint32_t chain;
		uint32_t wal_sequence;
		uint64_t wal_end;
		shared_state_header state;
		volatile uint32_t checkpointing;
		volatile uint32_t readers [ SHARED_READERS ];
		volatile uint32_t reader_generations [ SHARED_READERS ];

	};

	struct shared_segment {

		uint32_t pages [ SHARED_SEGMENT_FRAMES ];
		uint32_t previous [ SHARED_SEGMENT_FRAMES ];
		volatile uint32_t hash [ SHARED_HASH_SLOTS ];

	};

	struct shared_snapshot {

		uint32_t generation;
		uint32_t frames;
		uint32_t backfilled;
		uint32_t salt [ 2 ];
		uint32_t chain;
		uint32_t wal_sequence;
		uint64_t wal_end;
		shared_state_header state;

	};

	struct shared_view {

		void* file;
		shared_header* header;
		void* header_mapping;
		shared_segment* segments [ SHARED_SEGMENTS ];
		void* segment_mappings [ SHARED_SEGMENTS ];
		int32_t slot;
		bool present;

	};

	bool open_shared ( shared_view& out, const char* path, bool& first );

	void share_presence ( shared_view& item );

	void close_shared ( shared_view& item );

	void register_reader ( shared_view& item, shared_snapshot& out );

	void release_reader ( shared_view& item );

	void begin_checkpointing ( shared_view& item );

	void end_checkpointing ( shared_view& item );

	bool shared_segment_at ( shared_view& item, uint32_t index, shared_segment*& out );

	void read_snapshot ( const shared_view& item, shared_snapshot& out );

	void publish_snapshot ( shared_view& item, const shared_snapshot& value );

	bool shared_find ( shared_view& item, uint32_t page, uint32_t limit, uint32_t& frame );

	bool shared_append ( shared_view& item, uint32_t frame, uint32_t page );

	bool shared_clear ( shared_view& item, uint32_t frames );

	uint32_t oldest_reader ( shared_view& item, uint32_t generation, uint32_t frames, bool& others );

	bool take_write_lock ( shared_view& item, uint32_t milliseconds );

	void drop_write_lock ( shared_view& item );

	bool alone ( shared_view& item );

}
