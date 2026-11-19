#pragma once

#include "base/file/file.hh"
#include "base/bytes/bytes.hh"

#define PAGE_SIZE 4096
#define PAGE_CHECK 4
#define PAGE_USABLE ( PAGE_SIZE - PAGE_CHECK )
#define PAGE_MIX 0x9E3779B1u
#define PAGER_CACHE 2048
#define PAGER_BUCKETS 4096
#define HEADER_MAGIC "Strata format 1"
#define PAGER_PROBLEM_ROOM 160

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
		page_frame* chain;
		uint8_t* data;

	};

	struct pager_stats {

		uint64_t reads;
		uint64_t writes;
		uint64_t hits;
		uint64_t commits;

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
		pager_error error;
		char problem [ PAGER_PROBLEM_ROOM ];
		pager_stats stats;

	};

	bool open_pager ( pager& out, const char* path, uint32_t capacity = PAGER_CACHE );

	void close_pager ( pager& item );

	page_frame* get_page ( pager& item, uint32_t number );

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
