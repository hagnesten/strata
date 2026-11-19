#include "storage/btree/btree.hh"

namespace storage {

	uint64_t estimate_entries ( pager& owner, uint32_t root ) {

		page_summary* summary = base::make < page_summary > ( 1 );

		if ( !summary )
			return 1000;

		uint64_t estimate = 1;
		uint32_t page = root;

		for ( uint32_t depth = 0; depth < BTREE_DEPTH && page; depth++ ) {

			if ( !inspect_page ( owner, page, *summary ) ) {

				estimate = 1000;

				break;

			}

			if ( !summary->child_count ) {

				estimate *= summary->cells;

				break;

			}

			estimate *= summary->child_count;
			page = summary->children [ summary->child_count / 2 ];

		}

		base::release ( summary );

		return estimate;

	}

}