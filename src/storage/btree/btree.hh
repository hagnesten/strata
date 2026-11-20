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

	struct build_key {

		const uint8_t* data;
		uint32_t size;

	};

	struct build_node {

		uint32_t page;
		const uint8_t* last;
		uint32_t last_size;

	};

	struct build_level {

		build_node* nodes;
		uint32_t count;
		uint32_t room;

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
		const key_probe* probe;

	};

	struct remove_job {

		bool table;
		int64_t rowid;
		const uint8_t* key;
		size_t key_size;
		const key_probe* probe;

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

	enum scan_outcome : uint32_t {

		SCAN_REJECT,
		SCAN_TAKE,
		SCAN_STOP,
		SCAN_FAIL,
		SCAN_END,

	};

	typedef scan_outcome ( *row_filter ) ( void* context, int64_t rowid, const uint8_t* data, size_t size );

	struct scan_result {

		scan_outcome outcome;
		int64_t rowid;
		const uint8_t* data;
		size_t size;
		uint32_t page;
		uint32_t slot;
		uint64_t examined;

	};

	enum seek_match : uint8_t {

		SEEK_UNKNOWN,
		SEEK_EQUAL,
		SEEK_NONE,

	};

	struct cursor {

		btree tree;
		uint32_t page;
		uint32_t slot;
		bool valid;
		seek_match match;
		page_frame* frame;

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

	bool table_lookup ( btree& tree, int64_t rowid, base::arena& memory, const uint8_t*& data, size_t& size, bool& found, uint32_t* page = nullptr, uint32_t* where = nullptr );

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

	bool cursor_view ( cursor& at, base::arena& memory, int64_t& rowid, const uint8_t*& data, size_t& size, page_frame*& held );

	bool cursor_scan ( cursor& at, base::arena& memory, row_filter filter, void* context, scan_result& out );

	bool table_replace_at ( btree& tree, uint32_t number, uint32_t slot, int64_t rowid, const uint8_t* payload, size_t size, bool& done );

	bool check_btree ( pager& owner, uint32_t root, bool index, tree_report& report );

	bool copy_btree ( pager& from, uint32_t root, bool index, pager& to, uint32_t& made );

	bool fill_page ( uint8_t* page, uint8_t kind, const cell_span* cells, uint32_t count, uint32_t a, uint32_t b );

	bool key_body ( pager& owner, const uint8_t* data, size_t size, base::text& out );

	bool build_index ( btree& tree, const build_key* keys, uint32_t count );

	uint64_t estimate_entries ( pager& owner, uint32_t root );

}
