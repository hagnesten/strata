#include "test/harness/harness.hh"
#include "storage/pager/pager.hh"
#include "base/hash/crc32c.hh"

namespace test {

	static void stamp ( uint8_t* data, uint32_t number ) {

		for ( uint32_t i = 1; i < 64; i++ )
			data [ i ] = static_cast < uint8_t > ( number * 7 + i );

	}

	static bool stamped ( const uint8_t* data, uint32_t number ) {

		for ( uint32_t i = 1; i < 64; i++ )
			if ( data [ i ] != static_cast < uint8_t > ( number * 7 + i ) )
				return false;

		return true;

	}

	static void checksum_tests ( ) {

		uint8_t* data = base::make < uint8_t > ( 9000 );
		test_random random = { 0xC0FFEEull };
		bool same = data != nullptr;

		for ( uint32_t i = 0; data && i < 9000; i++ )
			data [ i ] = static_cast < uint8_t > ( random.next ( ) );

		for ( uint32_t round = 0; data && round < 3000 && same; round++ ) {

			uint32_t offset = random.below ( 64 );
			uint32_t size = round < 1000 ? round : random.below ( 9000 - 64 );
			uint32_t start = round & 1 ? static_cast < uint32_t > ( random.next ( ) ) : 0;

			same = base::crc32c ( data + offset, size, start ) == base::crc32c_portable ( data + offset, size, start );

		}

		expect ( same, "the fast checksum matches the portable one for every length and start" );
		expect ( base::crc32c ( "123456789", 9 ) == 0xE3069283u, "CRC-32C of 123456789 is E3069283" );
		base::release ( data );

	}

	void pager_tests ( ) {

		section ( "pager" );
		checksum_tests ( );

		char path [ FILE_PATH_ROOM ];

		if ( !expect ( scratch_path ( path, sizeof ( path ), "strata-test-pager.sdb" ), "a scratch path exists" ) )
			return;

		forget_database ( path );

		storage::pager item;

		expect ( storage::open_pager ( item, path ), "a new database opens" );
		expect ( item.header.page_count == 1, "a new database holds only its header" );

		bool written = true;

		for ( uint32_t i = 0; i < 3000 && written; i++ ) {

			storage::page_frame* frame = storage::new_page ( item, storage::PAGE_TABLE_LEAF );

			written = frame != nullptr;

			if ( frame ) {

				stamp ( frame->data, frame->number );
				storage::release_page ( frame );

			}

		}

		expect ( written, "3000 new pages are made" );
		expect ( storage::commit_pager ( item ), "they commit" );
		storage::close_pager ( item );

		expect ( storage::open_pager ( item, path, 64 ), "the database opens again with a small cache" );
		expect ( item.header.page_count == 3001, "the page count survives" );

		bool intact = true;
		uint32_t most_frames = 0;

		for ( uint32_t number = 2; number <= 3001 && intact; number++ ) {

			storage::page_frame* frame = storage::get_page ( item, number );

			intact = frame && frame->data [ 0 ] == storage::PAGE_TABLE_LEAF && stamped ( frame->data, number );
			storage::release_page ( frame );
			most_frames = base::max ( most_frames, item.frame_count );

		}

		expect ( intact, "every page reads back as written" );
		expect ( most_frames <= 64, "the cache stays within its limit" );

		for ( uint32_t number = 2; number <= 1001; number++ )
			storage::free_page ( item, number );

		expect ( item.header.free_count == 1000, "1000 pages go to the free list" );
		expect ( storage::commit_pager ( item ), "freeing commits" );

		bool reused = true;

		for ( uint32_t i = 0; i < 1000 && reused; i++ ) {

			storage::page_frame* frame = storage::new_page ( item, storage::PAGE_INDEX_LEAF );

			reused = frame && frame->number <= 1001;
			storage::release_page ( frame );

		}

		expect ( reused && item.header.page_count == 3001, "new pages come from the free list before the file grows" );
		expect ( item.header.free_count == 0, "the free list empties" );
		storage::rollback_pager ( item );
		expect ( item.header.free_count == 1000 && item.header.page_count == 3001, "rollback restores the header" );

		storage::page_frame* frame = storage::get_page ( item, 2000 );

		if ( frame ) {

			frame->data [ 10 ] ^= 0xFF;
			storage::dirty_page ( frame );
			storage::release_page ( frame );

		}

		storage::rollback_pager ( item );
		frame = storage::get_page ( item, 2000 );
		expect ( frame && stamped ( frame->data, 2000 ), "rollback throws away a changed page" );
		storage::release_page ( frame );
		storage::close_pager ( item );

		flip_byte ( path, static_cast < uint64_t > ( 2500 - 1 ) * PAGE_SIZE + 33 );
		expect ( storage::open_pager ( item, path ), "a database with one bad page still opens" );
		expect ( storage::get_page ( item, 2500 ) == nullptr && item.error == storage::PAGER_CORRUPT, "the bad page is reported as corrupt" );

		frame = storage::get_page ( item, 2501 );
		expect ( frame != nullptr, "its neighbour still reads" );
		storage::release_page ( frame );
		expect ( storage::get_page ( item, 5000 ) == nullptr, "a page past the end is refused" );
		storage::close_pager ( item );

		flip_byte ( path, 20 );
		expect ( !storage::open_pager ( item, path ), "a damaged header is refused" );
		forget_database ( path );

	}

}
