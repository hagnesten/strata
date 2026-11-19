#pragma once

#include "storage/pager/pager.hh"
#include "storage/record/record.hh"
#include "base/arena/arena.hh"

#define BTREE_HEADER 16
#define BTREE_SPACE ( PAGE_USABLE - BTREE_HEADER )
#define BTREE_MAX_LOCAL 995
#define BTREE_MIN_LOCAL 240
#define OVERFLOW_HEADER 12
#define OVERFLOW_ROOM ( PAGE_USABLE - OVERFLOW_HEADER )
#define BTREE_CELLS 1024
#define BTREE_DEPTH 32
#define BTREE_UNDERFULL ( BTREE_SPACE / 3 )

namespace storage {

	struct btree {

		pager* owner;
		uint32_t root;
		bool index;

	};

	struct cell_span {

		const uint8_t* data;
		uint32_t size;

	};

	struct cell_info {

		uint32_t child;
		int64_t rowid;
		uint64_t payload;
		uint32_t local;
		const uint8_t* local_data;
		uint32_t overflow;
		uint32_t size;

	};

	struct insert_job {

		bool table;
		int64_t rowid;
		const uint8_t* key;
		size_t key_size;
		const uint8_t* cell;
		uint32_t cell_size;
		bool replace;
		bool existed;

	};

	struct remove_job {

		bool table;
		int64_t rowid;
		const uint8_t* key;
		size_t key_size;

	};

	struct leaf_trail {

		uint32_t* pages;
		uint32_t count;
		uint32_t room;

	};

	struct split_result {

		bool happened;
		uint32_t right;
		int64_t rowid;
		base::text body;

	};

	struct remove_result {

		bool found;
		bool underfull;

	};

	struct cursor {

		btree tree;
		uint32_t page;
		uint32_t slot;
		bool valid;

	};

	struct tree_report {

		uint32_t pages;
		uint32_t leaves;
		uint32_t overflow;
		uint32_t depth;
		uint64_t entries;
		uint64_t used_bytes;
		bool fine;
		base::text problem;

	};

	struct page_summary {

		uint32_t number;
		uint8_t kind;
		uint32_t cells;
		uint32_t used;
		uint32_t free;
		uint32_t children [ BTREE_CELLS + 1 ];
		uint32_t child_count;
		uint32_t overflow_cells;
		bool has_rowids;
		int64_t low_rowid;
		int64_t high_rowid;
		uint32_t next;
		uint32_t previous;

	};

	bool inspect_page ( pager& owner, uint32_t number, page_summary& out );

	bool read_page_bytes ( pager& owner, uint32_t number, uint8_t* out );

	bool create_btree ( pager& owner, bool index, uint32_t& root );

	bool drop_btree ( pager& owner, uint32_t root );

	bool table_insert ( btree& tree, int64_t rowid, const uint8_t* payload, size_t size, bool replace, bool& existed );

	bool table_delete ( btree& tree, int64_t rowid, bool& found );

	bool table_find ( btree& tree, int64_t rowid, base::text& payload, bool& found );

	bool table_lookup ( btree& tree, int64_t rowid, base::arena& memory, const uint8_t*& data, size_t& size, bool& found );

	bool table_last ( btree& tree, int64_t& rowid, bool& empty );

	bool index_insert ( btree& tree, const uint8_t* key, size_t size, bool& existed );

	bool index_delete ( btree& tree, const uint8_t* key, size_t size, bool& found );

	bool cursor_first ( cursor& at, btree& tree );

	bool cursor_seek_rowid ( cursor& at, btree& tree, int64_t rowid );

	bool cursor_seek_key ( cursor& at, btree& tree, const uint8_t* key, size_t size, uint32_t columns );

	bool cursor_next ( cursor& at );

	bool cursor_rowid ( cursor& at, int64_t& rowid );

	bool cursor_payload ( cursor& at, base::text& out );

	bool cursor_take ( cursor& at, base::arena& memory, int64_t& rowid, const uint8_t*& data, size_t& size );

	bool check_btree ( pager& owner, uint32_t root, bool index, tree_report& report );

	bool copy_btree ( pager& from, uint32_t root, bool index, pager& to, uint32_t& made );

	uint64_t estimate_entries ( pager& owner, uint32_t root );

}
