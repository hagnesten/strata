#include "storage/pager/pager.hh"
#include "base/hash/crc32c.hh"

namespace storage {

	uint32_t page_checksum ( const uint8_t* data, uint32_t number ) {

		return base::crc32c ( data, PAGE_USABLE ) ^ number * PAGE_MIX;

	}

	void seal_page ( uint8_t* data, uint32_t number ) {

		base::write32 ( data + PAGE_USABLE, page_checksum ( data, number ) );

	}

	bool page_intact ( const uint8_t* data, uint32_t number ) {

		return base::read32 ( data + PAGE_USABLE ) == page_checksum ( data, number );

	}

	static void fail ( pager& item, pager_error error, const char* first, uint64_t number, const char* rest ) {

		base::text line;

		line.add ( first );

		if ( rest ) {

			line.number ( number ).add ( rest );

		}

		item.error = error;
		base::copy_text ( item.problem, sizeof ( item.problem ), line.value ( ) );

	}

	static void encode_header ( const database_header& header, uint8_t* data ) {

		base::fill ( data, 0, PAGE_USABLE );
		data [ 0 ] = PAGE_HEADER;
		base::copy ( data + 1, HEADER_MAGIC, 15 );
		base::write32 ( data + 16, header.page_size );
		base::write32 ( data + 20, header.page_count );
		base::write32 ( data + 24, header.free_head );
		base::write32 ( data + 28, header.free_count );
		base::write32 ( data + 32, header.schema_root );
		base::write32 ( data + 36, header.schema_version );
		base::write64 ( data + 40, header.change );

	}

	static bool decode_header ( const uint8_t* data, database_header& out ) {

		if ( data [ 0 ] != PAGE_HEADER || !base::same_bytes ( data + 1, HEADER_MAGIC, 15 ) )
			return false;

		out.page_size = base::read32 ( data + 16 );
		out.page_count = base::read32 ( data + 20 );
		out.free_head = base::read32 ( data + 24 );
		out.free_count = base::read32 ( data + 28 );
		out.schema_root = base::read32 ( data + 32 );
		out.schema_version = base::read32 ( data + 36 );
		out.change = base::read64 ( data + 40 );

		return out.page_size == PAGE_SIZE && out.page_count >= 1 && out.free_head < out.page_count + 1 && out.free_count < out.page_count && out.schema_root <= out.page_count;

	}

	static page_frame** bucket_of ( pager& item, uint32_t number ) {

		return &item.buckets [ number * 2654435761u >> 20 & ( PAGER_BUCKETS - 1 ) ];

	}

	static page_frame* find_frame ( pager& item, uint32_t number ) {

		for ( page_frame* at = *bucket_of ( item, number ); at; at = at->chain )
			if ( at->number == number )
				return at;

		return nullptr;

	}

	static void unlink_frame ( pager& item, page_frame* frame ) {

		page_frame** at = bucket_of ( item, frame->number );

		while ( *at && *at != frame )
			at = &( *at )->chain;

		if ( *at )
			*at = frame->chain;

		frame->chain = nullptr;

	}

	static void link_frame ( pager& item, page_frame* frame ) {

		page_frame** at = bucket_of ( item, frame->number );

		frame->chain = *at;
		*at = frame;

	}

	static page_frame* spare_frame ( pager& item ) {

		if ( item.frame_count >= item.capacity ) {

			for ( uint32_t pass = 0; pass < item.frame_count * 2; pass++ ) {

				page_frame* frame = item.frames [ item.hand ];

				item.hand = ( item.hand + 1 ) % item.frame_count;

				if ( frame->pins || frame->dirty )
					continue;

				if ( frame->recent ) {

					frame->recent = false;

					continue;

				}

				unlink_frame ( item, frame );
				frame->number = 0;

				return frame;

			}

		}

		if ( item.frame_count == item.frame_room ) {

			uint32_t room = item.frame_room ? item.frame_room * 2 : 64;
			page_frame** grown = base::make < page_frame* > ( room );

			if ( !grown )
				return nullptr;

			if ( item.frames )
				base::copy ( grown, item.frames, sizeof ( page_frame* ) * item.frame_count );

			base::release ( item.frames );
			item.frames = grown;
			item.frame_room = room;

		}

		page_frame* frame = base::make < page_frame > ( 1 );
		uint8_t* data = frame ? base::make < uint8_t > ( PAGE_SIZE ) : nullptr;

		if ( !frame || !data ) {

			base::release ( frame );
			base::release ( data );

			return nullptr;

		}

		frame->data = data;
		item.frames [ item.frame_count++ ] = frame;

		return frame;

	}

	bool open_pager ( pager& out, const char* path, uint32_t capacity ) {

		base::fill ( &out, 0, sizeof ( out ) );
		out.capacity = base::max < uint32_t > ( capacity, 16 );
		base::copy_text ( out.path, sizeof ( out.path ), path );

		if ( !base::open_file ( out.file, path, true ) ) {

			fail ( out, PAGER_IO, "the database file could not be opened", 0, nullptr );

			return false;

		}

		uint8_t* page = base::make < uint8_t > ( PAGE_SIZE );

		if ( !page ) {

			fail ( out, PAGER_MEMORY, "out of memory", 0, nullptr );

			return false;

		}

		uint64_t size = base::file_size ( out.file );
		bool fine = true;

		if ( !size ) {

			out.header.page_size = PAGE_SIZE;
			out.header.page_count = 1;
			encode_header ( out.header, page );
			seal_page ( page, 1 );
			fine = base::write_at ( out.file, 0, page, PAGE_SIZE ) && base::sync_file ( out.file );

			if ( !fine )
				fail ( out, PAGER_IO, "the new database could not be written", 0, nullptr );

		} else if ( size < PAGE_SIZE || !base::read_at ( out.file, 0, page, PAGE_SIZE ) ) {

			fail ( out, PAGER_FORMAT, "this is not a Strata database, it is too short", 0, nullptr );
			fine = false;

		} else if ( !decode_header ( page, out.header ) ) {

			fail ( out, PAGER_FORMAT, "this is not a Strata database, or it is from a newer version", 0, nullptr );
			fine = false;

		} else if ( !page_intact ( page, 1 ) ) {

			fail ( out, PAGER_CORRUPT, "the database header failed its checksum", 0, nullptr );
			fine = false;

		} else if ( static_cast < uint64_t > ( out.header.page_count ) * PAGE_SIZE > size ) {

			fail ( out, PAGER_CORRUPT, "the database file is shorter than its header says, ", out.header.page_count, " pages expected" );
			fine = false;

		}

		base::release ( page );
		out.committed = out.header;

		if ( !fine )
			base::close_file ( out.file );

		return fine;

	}

	void close_pager ( pager& item ) {

		for ( uint32_t i = 0; i < item.frame_count; i++ ) {

			base::release ( item.frames [ i ]->data );
			base::release ( item.frames [ i ] );

		}

		base::release ( item.frames );
		base::close_file ( item.file );
		item.frames = nullptr;
		item.frame_count = 0;
		item.frame_room = 0;

	}

	page_frame* get_page ( pager& item, uint32_t number ) {

		if ( !number || number > item.header.page_count ) {

			fail ( item, PAGER_CORRUPT, "a link points at page ", number, ", which is outside the database" );

			return nullptr;

		}

		page_frame* frame = find_frame ( item, number );

		if ( frame ) {

			frame->pins++;
			frame->recent = true;
			item.stats.hits++;

			return frame;

		}

		frame = spare_frame ( item );

		if ( !frame ) {

			fail ( item, PAGER_MEMORY, "out of memory for page ", number, "" );

			return nullptr;

		}

		if ( !base::read_at ( item.file, static_cast < uint64_t > ( number - 1 ) * PAGE_SIZE, frame->data, PAGE_SIZE ) ) {

			fail ( item, PAGER_IO, "page ", number, " could not be read" );
			frame->number = 0;

			return nullptr;

		}

		if ( !page_intact ( frame->data, number ) ) {

			fail ( item, PAGER_CORRUPT, "page ", number, " failed its checksum" );
			frame->number = 0;

			return nullptr;

		}

		item.stats.reads++;
		frame->number = number;
		frame->pins = 1;
		frame->dirty = false;
		frame->recent = true;
		link_frame ( item, frame );

		return frame;

	}

	page_frame* new_page ( pager& item, page_kind kind ) {

		page_frame* frame = nullptr;

		if ( item.header.free_head ) {

			uint32_t number = item.header.free_head;

			frame = get_page ( item, number );

			if ( !frame )
				return nullptr;

			if ( frame->data [ 0 ] != PAGE_FREE ) {

				fail ( item, PAGER_CORRUPT, "the free list points at page ", number, ", which is in use" );
				release_page ( frame );

				return nullptr;

			}

			item.header.free_head = base::read32 ( frame->data + 4 );
			item.header.free_count--;

		} else {

			frame = spare_frame ( item );

			if ( !frame ) {

				fail ( item, PAGER_MEMORY, "out of memory for a new page", 0, nullptr );

				return nullptr;

			}

			item.header.page_count++;
			frame->number = item.header.page_count;
			frame->pins = 1;
			frame->recent = true;
			link_frame ( item, frame );

		}

		base::fill ( frame->data, 0, PAGE_SIZE );
		frame->data [ 0 ] = kind;
		frame->dirty = true;

		return frame;

	}

	void release_page ( page_frame* frame ) {

		if ( frame && frame->pins )
			frame->pins--;

	}

	void dirty_page ( page_frame* frame ) {

		frame->dirty = true;

	}

	bool free_page ( pager& item, uint32_t number ) {

		if ( number <= 1 || number > item.header.page_count ) {

			fail ( item, PAGER_CORRUPT, "page ", number, " cannot be freed" );

			return false;

		}

		page_frame* frame = get_page ( item, number );

		if ( !frame )
			return false;

		base::fill ( frame->data, 0, PAGE_SIZE );
		frame->data [ 0 ] = PAGE_FREE;
		base::write32 ( frame->data + 4, item.header.free_head );
		frame->dirty = true;
		item.header.free_head = number;
		item.header.free_count++;
		release_page ( frame );

		return true;

	}

	bool has_changes ( const pager& item ) {

		if ( !base::same_bytes ( &item.header, &item.committed, sizeof ( item.header ) ) )
			return true;

		for ( uint32_t i = 0; i < item.frame_count; i++ )
			if ( item.frames [ i ]->number && item.frames [ i ]->dirty )
				return true;

		return false;

	}

	bool commit_pager ( pager& item ) {

		if ( !has_changes ( item ) )
			return true;

		item.header.change++;

		uint8_t* header = base::make < uint8_t > ( PAGE_SIZE );

		if ( !header ) {

			fail ( item, PAGER_MEMORY, "out of memory while committing", 0, nullptr );

			return false;

		}

		for ( uint32_t i = 0; i < item.frame_count; i++ ) {

			page_frame* frame = item.frames [ i ];

			if ( !frame->number || !frame->dirty )
				continue;

			seal_page ( frame->data, frame->number );

			if ( !base::write_at ( item.file, static_cast < uint64_t > ( frame->number - 1 ) * PAGE_SIZE, frame->data, PAGE_SIZE ) ) {

				fail ( item, PAGER_IO, "page ", frame->number, " could not be written" );
				base::release ( header );

				return false;

			}

			item.stats.writes++;

		}

		encode_header ( item.header, header );
		seal_page ( header, 1 );

		bool fine = base::write_at ( item.file, 0, header, PAGE_SIZE ) && base::sync_file ( item.file );

		base::release ( header );

		if ( !fine ) {

			fail ( item, PAGER_IO, "the commit could not be written to disk", 0, nullptr );

			return false;

		}

		for ( uint32_t i = 0; i < item.frame_count; i++ )
			item.frames [ i ]->dirty = false;

		page_frame* first = find_frame ( item, 1 );

		if ( first ) {

			unlink_frame ( item, first );
			first->number = 0;

		}

		item.committed = item.header;
		item.stats.commits++;

		return true;

	}

	void rollback_pager ( pager& item ) {

		for ( uint32_t i = 0; i < item.frame_count; i++ ) {

			page_frame* frame = item.frames [ i ];

			if ( !frame->number || !frame->dirty )
				continue;

			unlink_frame ( item, frame );
			frame->number = 0;
			frame->dirty = false;
			frame->pins = 0;

		}

		item.header = item.committed;
		item.error = PAGER_FINE;
		item.problem [ 0 ] = 0;

	}

}
