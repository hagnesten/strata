#pragma once

#include "base/file/file.hh"
#include "base/bytes/bytes.hh"
#include "storage/shared/shared.hh"

#define PAGE_SIZE 4096
#define PAGE_CHECK 4
#define PAGE_USABLE ( PAGE_SIZE - PAGE_CHECK )
#define PAGE_MIX 0x9E3779B1u
#define PAGER_CACHE 2048
#define PAGER_BUCKETS 4096
#define HEADER_MAGIC "Strata format 1"
#define PAGER_PROBLEM_ROOM 160
#define WAL_MAGIC "StrataWL"
#define WAL_HEADER 32
#define WAL_FRAME_HEADER 24
#define WAL_FRAME ( WAL_FRAME_HEADER + PAGE_SIZE )
#define WAL_CHECKPOINT_FRAMES 1000
#define WAL_BATCH 64
#define PAGER_TAILS 8
#define PAGER_LEAVES 8
#define WAL_KEEP_BYTES ( 64ull * 1024 * 1024 )

namespace storage {

	enum page_kind : uint8_t {

		PAGE_HEADER = 1,
		PAGE_FREE = 2,
		PAGE_TABLE_LEAF = 10,
		PAGE_TABLE_INTERIOR = 11,
		PAGE_INDEX_LEAF = 12,
		PAGE_INDEX_INTERIOR = 13,
		PAGE_OVERFLOW = 20,

	};

	enum pager_error : uint8_t {

		PAGER_FINE,
		PAGER_IO,
		PAGER_CORRUPT,
		PAGER_MEMORY,
		PAGER_FORMAT,
		PAGER_BUSY,

	};

	struct database_header {

		uint32_t page_size;
		uint32_t page_count;
		uint32_t free_head;
		uint32_t free_count;
		uint32_t schema_root;
		uint32_t schema_version;
		uint64_t change;

	};

	struct page_frame {

		uint32_t number;
		uint32_t pins;
		bool dirty;
		bool recent;
		uint8_t marked;
		uint32_t saved_epoch;
		uint32_t dirty_epoch;
		page_frame* chain;
		uint8_t* data;

	};

	struct pager_stats {

		uint64_t reads;
		uint64_t writes;
		uint64_t hits;
		uint64_t commits;
		uint64_t checkpoints;
		uint64_t recovered_frames;

	};

	struct wal_entry {

		uint32_t page;
		uint32_t frame;

	};

	struct wal_state {

		base::file_handle file;
		char path [ FILE_PATH_ROOM ];
		uint32_t checkpoint_frames;
		uint8_t* batch;
		shared_view shared;
		shared_snapshot snapshot;
		bool reading;
		bool writing;
		bool read_any;
		uint32_t busy_milliseconds;

	};

	struct page_copy {

		page_frame* frame;
		uint8_t* data;

	};

	struct pager_mark {

		uint32_t epoch;
		database_header header;
		page_copy* copies;
		uint32_t copy_count;
		uint32_t copy_room;

	};

	struct tail_hint {

		uint32_t root;
		uint32_t leaf;
		uint64_t shape;

	};

	struct leaf_hint {

		uint32_t root;
		uint32_t leaf;
		uint64_t layout;
		int32_t score;
		uint32_t tries;

	};

	struct mark_stack {

		pager_mark* marks;
		uint32_t count;
		uint32_t room;
		uint32_t epoch;

	};

	struct pager {

		base::file_handle file;
		char path [ FILE_PATH_ROOM ];
		database_header header;
		database_header committed;
		page_frame** frames;
		uint32_t frame_count;
		uint32_t frame_room;
		page_frame* buckets [ PAGER_BUCKETS ];
		uint32_t hand;
		uint32_t capacity;
		uint32_t grow_until;
		pager_error error;
		char problem [ PAGER_PROBLEM_ROOM ];
		pager_stats stats;
		wal_state wal;
		bool broken;
		mark_stack marks;
		base::text cell_bytes;
		uint64_t shape;
		tail_hint tails [ PAGER_TAILS ];
		uint32_t next_tail;
		uint64_t layout;
		leaf_hint leaves [ PAGER_LEAVES ];
		uint32_t next_leaf;

	};

	bool push_mark ( pager& item );

	bool save_for_mark ( pager& item, page_frame* frame );

	bool change_page ( pager& item, page_frame* frame );

	bool release_mark ( pager& item, uint32_t depth );

	void undo_to_mark ( pager& item, uint32_t depth, bool keep );

	void clear_marks ( pager& item );

	bool begin_statement ( pager& item );

	bool end_statement ( pager& item );

	void discard_frame ( pager& item, page_frame* frame );

	void undo_statement ( pager& item );

	bool open_wal ( pager& item, uint8_t* page, bool& recovered );

	bool wal_find ( pager& item, uint32_t number, uint64_t& offset );

	bool wal_still_valid ( pager& item );

	bool begin_read ( pager& item );

	void end_read ( pager& item );

	bool begin_write ( pager& item );

	void end_write ( pager& item );

	bool pager_alone ( pager& item );

	bool wal_commit ( pager& item, const uint8_t* header_page );

	bool checkpoint ( pager& item );

	void close_wal ( pager& item );

	void fail ( pager& item, pager_error error, const char* first, uint64_t number, const char* rest );

	void encode_header ( const database_header& header, uint8_t* data );

	bool decode_header ( const uint8_t* data, database_header& out );

	bool open_pager ( pager& out, const char* path, uint32_t capacity = PAGER_CACHE );

	void close_pager ( pager& item );

	page_frame* get_page ( pager& item, uint32_t number );

	page_frame* cached_frame ( pager& item, uint32_t number );

	page_frame* new_page ( pager& item, page_kind kind );

	void release_page ( page_frame* frame );

	void dirty_page ( page_frame* frame );

	bool free_page ( pager& item, uint32_t number );

	bool commit_pager ( pager& item );

	void rollback_pager ( pager& item );

	bool has_changes ( const pager& item );

	uint32_t page_checksum ( const uint8_t* data, uint32_t number );

	void seal_page ( uint8_t* data, uint32_t number );

	bool page_intact ( const uint8_t* data, uint32_t number );

}
