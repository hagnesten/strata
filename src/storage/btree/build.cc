#include "storage/btree/btree.hh"

namespace storage {

	static uint32_t body_size ( uint32_t size ) {

		uint32_t local = size <= BTREE_MAX_LOCAL ? size : BTREE_MIN_LOCAL;

		return static_cast < uint32_t > ( base::varint_size ( size ) ) + local + ( size > local ? 4 : 0 );

	}

	static bool add_node ( build_level& level, uint32_t page, const uint8_t* last, uint32_t last_size ) {

		if ( level.count == level.room ) {

			uint32_t room = level.room ? level.room * 2 : 64;
			build_node* made = base::make < build_node > ( room );

			if ( !made )
				return false;

			if ( level.nodes )
				base::copy ( made, level.nodes, sizeof ( build_node ) * level.count );

			base::release ( level.nodes );
			level.nodes = made;
			level.room = room;

		}

		level.nodes [ level.count++ ] = { page, last, last_size };

		return true;

	}

	static bool put_body ( pager& owner, const uint8_t* data, uint32_t size, uint8_t* out, uint32_t& used ) {

		if ( size <= BTREE_MAX_LOCAL ) {

			used += static_cast < uint32_t > ( base::write_varint ( out + used, size ) );

			if ( size )
				base::copy ( out + used, data, size );

			used += size;

			return true;

		}

		base::text body;

		if ( !key_body ( owner, data, size, body ) )
			return false;

		base::copy ( out + used, body.data, body.size );
		used += static_cast < uint32_t > ( body.size );

		return true;

	}

	static bool write_root ( pager& owner, uint32_t root, uint8_t kind, const cell_span* cells, uint32_t count, uint32_t a ) {

		page_frame* frame = get_page ( owner, root );

		if ( !frame )
			return false;

		bool fine = change_page ( owner, frame ) && fill_page ( frame->data, kind, cells, count, a, 0 );

		if ( fine )
			dirty_page ( frame );

		release_page ( frame );

		return fine;

	}

	static bool build_leaves ( btree& tree, const build_key* keys, uint32_t count, uint8_t* scratch, cell_span* cells, build_level& out ) {

		pager& owner = *tree.owner;
		uint32_t total = 0;
		bool single = true;

		for ( uint32_t i = 0; i < count && single; i++ ) {

			total += body_size ( keys [ i ].size ) + 2;
			single = total <= BTREE_SPACE && i < BTREE_CELLS;

		}

		page_frame* current = nullptr;
		uint32_t previous = 0;
		uint32_t start = 0;

		while ( start < count ) {

			uint32_t used = 0;
			uint32_t taken = 0;
			uint32_t space = 0;

			while ( start + taken < count && taken < BTREE_CELLS ) {

				uint32_t size = body_size ( keys [ start + taken ].size ) + 2;

				if ( space + size > BTREE_SPACE )
					break;

				space += size;
				cells [ taken ].data = scratch + used;

				uint32_t before = used;

				if ( !put_body ( owner, keys [ start + taken ].data, keys [ start + taken ].size, scratch, used ) ) {

					release_page ( current );

					return false;

				}

				cells [ taken ].size = used - before;
				taken++;

			}

			if ( !taken ) {

				release_page ( current );

				return false;

			}

			const build_key& last = keys [ start + taken - 1 ];

			if ( single )
				return write_root ( owner, tree.root, PAGE_INDEX_LEAF, cells, taken, 0 ) && add_node ( out, tree.root, last.data, last.size );

			if ( !current && !( current = new_page ( owner, PAGE_INDEX_LEAF ) ) )
				return false;

			page_frame* next = nullptr;

			if ( start + taken < count && !( next = new_page ( owner, PAGE_INDEX_LEAF ) ) ) {

				release_page ( current );

				return false;

			}

			bool fine = fill_page ( current->data, PAGE_INDEX_LEAF, cells, taken, next ? next->number : 0, previous ) && add_node ( out, current->number, last.data, last.size );

			dirty_page ( current );
			previous = current->number;
			release_page ( current );
			current = next;
			start += taken;

			if ( !fine ) {

				release_page ( current );

				return false;

			}

		}

		return true;

	}

	static bool build_interior ( btree& tree, const build_level& below, bool& top, uint8_t* scratch, cell_span* cells, build_level& out ) {

		pager& owner = *tree.owner;
		uint32_t* ends = base::make < uint32_t > ( below.count + 1 );
		uint32_t groups = 0;

		if ( !ends )
			return false;

		for ( uint32_t s = 0; s < below.count; ) {

			uint32_t e = s;
			uint32_t space = 0;

			while ( e + 1 < below.count && e - s + 1 < BTREE_CELLS ) {

				uint32_t size = 4 + body_size ( below.nodes [ e ].last_size ) + 2;

				if ( space + size > BTREE_SPACE )
					break;

				space += size;
				e++;

			}

			ends [ groups++ ] = e;
			s = e + 1;

		}

		if ( groups > 1 && ends [ groups - 1 ] == ends [ groups - 2 ] + 1 )
			ends [ groups - 2 ]--;

		top = groups == 1;

		bool fine = true;
		uint32_t start = 0;

		for ( uint32_t g = 0; g < groups && fine; g++ ) {

			uint32_t end = ends [ g ];
			uint32_t used = 0;
			uint32_t taken = 0;

			for ( uint32_t c = start; c < end && fine; c++ ) {

				uint32_t before = used;

				base::write32 ( scratch + used, below.nodes [ c ].page );
				used += 4;
				fine = put_body ( owner, below.nodes [ c ].last, below.nodes [ c ].last_size, scratch, used );
				cells [ taken ].data = scratch + before;
				cells [ taken ].size = used - before;
				taken++;

			}

			const build_node& right = below.nodes [ end ];

			if ( fine && top ) {

				fine = write_root ( owner, tree.root, PAGE_INDEX_INTERIOR, cells, taken, right.page ) && add_node ( out, tree.root, right.last, right.last_size );

			} else if ( fine ) {

				page_frame* frame = new_page ( owner, PAGE_INDEX_INTERIOR );

				fine = frame && fill_page ( frame->data, PAGE_INDEX_INTERIOR, cells, taken, right.page, 0 ) && add_node ( out, frame->number, right.last, right.last_size );

				if ( frame ) {

					dirty_page ( frame );
					release_page ( frame );

				}

			}

			start = end + 1;

		}

		base::release ( ends );

		return fine;

	}

	bool build_index ( btree& tree, const build_key* keys, uint32_t count ) {

		if ( !count )
			return true;

		uint8_t* scratch = base::make < uint8_t > ( PAGE_SIZE * 2 );
		cell_span* cells = base::make < cell_span > ( BTREE_CELLS + 1 );
		build_level below = { };
		build_level above = { };
		bool fine = scratch && cells && build_leaves ( tree, keys, count, scratch, cells, below );
		bool top = below.count == 1;

		for ( uint32_t depth = 0; fine && !top; depth++ ) {

			if ( depth > BTREE_DEPTH ) {

				fine = false;

				break;

			}

			above.count = 0;
			fine = build_interior ( tree, below, top, scratch, cells, above );

			build_level held = below;

			below = above;
			above = held;

		}

		base::release ( below.nodes );
		base::release ( above.nodes );
		base::release ( scratch );
		base::release ( cells );

		return fine;

	}

}
