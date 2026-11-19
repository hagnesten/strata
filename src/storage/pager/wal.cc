#include "storage/pager/pager.hh"
#include "base/hash/crc32c.hh"

#include <windows.h>

namespace storage {

	static uint32_t slot_for ( uint32_t page, uint32_t room ) {

		return page * 2654435761u & ( room - 1 );

	}

	static bool index_put ( wal_state& wal, uint32_t page, uint64_t offset ) {

		if ( ( wal.index_used + 1 ) * 10 > wal.index_room * 7 ) {

			uint32_t room = wal.index_room ? wal.index_room * 2 : 1024;
			wal_entry* grown = base::make < wal_entry > ( room );

			if ( !grown )
				return false;

			for ( uint32_t i = 0; i < wal.index_room; i++ ) {

				if ( !wal.index [ i ].page )
					continue;

				uint32_t at = slot_for ( wal.index [ i ].page, room );

				while ( grown [ at ].page )
					at = ( at + 1 ) & ( room - 1 );

				grown [ at ] = wal.index [ i ];

			}

			base::release ( wal.index );
			wal.index = grown;
			wal.index_room = room;

		}

		uint32_t at = slot_for ( page, wal.index_room );

		while ( wal.index [ at ].page && wal.index [ at ].page != page )
			at = ( at + 1 ) & ( wal.index_room - 1 );

		if ( !wal.index [ at ].page )
			wal.index_used++;

		wal.index [ at ] = { page, offset };

		return true;

	}

	bool wal_find ( const pager& item, uint32_t number, uint64_t& offset ) {

		const wal_state& wal = item.wal;

		if ( !wal.index_used )
			return false;

		uint32_t at = slot_for ( number, wal.index_room );

		while ( wal.index [ at ].page ) {

			if ( wal.index [ at ].page == number ) {

				offset = wal.index [ at ].offset;

				return true;

			}

			at = ( at + 1 ) & ( wal.index_room - 1 );

		}

		return false;

	}

	static void index_clear ( wal_state& wal ) {

		if ( wal.index )
			base::fill ( wal.index, 0, sizeof ( wal_entry ) * wal.index_room );

		wal.index_used = 0;

	}

	static void encode_wal_header ( wal_state& wal, uint8_t* out ) {

		base::fill ( out, 0, WAL_HEADER );
		base::copy ( out, WAL_MAGIC, 8 );
		base::write32 ( out + 8, 1 );
		base::write32 ( out + 12, PAGE_SIZE );
		base::write32 ( out + 16, wal.salt [ 0 ] );
		base::write32 ( out + 20, wal.salt [ 1 ] );
		base::write32 ( out + 24, wal.sequence );
		base::write32 ( out + 28, base::crc32c ( out, 28 ) );
		wal.chain = base::read32 ( out + 28 );

	}

	static bool fresh_log ( pager& item ) {

		wal_state& wal = item.wal;
		uint8_t header [ WAL_HEADER ];
		LARGE_INTEGER tick = { };

		QueryPerformanceCounter ( &tick );
		wal.salt [ 0 ]++;
		wal.salt [ 1 ] = static_cast < uint32_t > ( tick.QuadPart ) ^ wal.salt [ 1 ] * 2654435761u ^ GetCurrentProcessId ( );
		wal.sequence++;
		encode_wal_header ( wal, header );

		bool shrink = base::file_size ( wal.file ) > WAL_KEEP_BYTES;

		if ( !base::write_at ( wal.file, 0, header, WAL_HEADER ) || ( shrink && !base::truncate_file ( wal.file, WAL_HEADER ) ) || !base::sync_file ( wal.file ) ) {

			fail ( item, PAGER_IO, "the log could not be reset", 0, nullptr );

			return false;

		}

		wal.end = WAL_HEADER;
		wal.frames = 0;
		index_clear ( wal );

		return true;

	}

	static bool recover ( pager& item ) {

		wal_state& wal = item.wal;
		uint8_t header [ WAL_HEADER ];
		uint64_t size = base::file_size ( wal.file );

		if ( size < WAL_HEADER || !base::read_at ( wal.file, 0, header, WAL_HEADER ) || !base::same_bytes ( header, WAL_MAGIC, 8 ) || base::read32 ( header + 8 ) != 1 || base::read32 ( header + 12 ) != PAGE_SIZE || base::read32 ( header + 28 ) != base::crc32c ( header, 28 ) )
			return fresh_log ( item );

		wal.salt [ 0 ] = base::read32 ( header + 16 );
		wal.salt [ 1 ] = base::read32 ( header + 20 );
		wal.sequence = base::read32 ( header + 24 );
		wal.chain = base::read32 ( header + 28 );

		uint8_t* frame = base::make < uint8_t > ( WAL_FRAME );
		uint32_t pending_room = 1024;
		wal_entry* pending = base::make < wal_entry > ( pending_room );

		if ( !frame || !pending ) {

			base::release ( frame );
			base::release ( pending );
			fail ( item, PAGER_MEMORY, "out of memory while reading the log", 0, nullptr );

			return false;

		}

		uint64_t offset = WAL_HEADER;
		uint64_t committed_end = WAL_HEADER;
		uint32_t committed_chain = wal.chain;
		uint32_t chain = wal.chain;
		uint32_t waiting = 0;
		uint32_t kept = 0;
		bool fine = true;

		while ( fine && offset + WAL_FRAME <= size && base::read_at ( wal.file, offset, frame, WAL_FRAME ) ) {

			uint32_t page = base::read32 ( frame );
			uint32_t commit = base::read32 ( frame + 4 );
			uint32_t check = base::crc32c ( frame + WAL_FRAME_HEADER, PAGE_SIZE, base::crc32c ( frame, 16, chain ) );

			if ( base::read32 ( frame + 8 ) != wal.salt [ 0 ] || base::read32 ( frame + 12 ) != wal.salt [ 1 ] || base::read32 ( frame + 16 ) != check || !page )
				break;

			if ( waiting == pending_room ) {

				wal_entry* grown = base::make < wal_entry > ( pending_room * 2 );

				if ( !grown ) {

					fine = false;

					break;

				}

				base::copy ( grown, pending, sizeof ( wal_entry ) * waiting );
				base::release ( pending );
				pending = grown;
				pending_room *= 2;

			}

			pending [ waiting++ ] = { page, offset };
			chain = check;
			offset += WAL_FRAME;

			if ( commit ) {

				for ( uint32_t i = 0; i < waiting && fine; i++ )
					fine = index_put ( wal, pending [ i ].page, pending [ i ].offset );

				kept += waiting;
				waiting = 0;
				committed_end = offset;
				committed_chain = chain;

			}

		}

		base::release ( frame );
		base::release ( pending );

		if ( !fine ) {

			fail ( item, PAGER_MEMORY, "out of memory while reading the log", 0, nullptr );

			return false;

		}

		wal.end = committed_end;
		wal.chain = committed_chain;
		wal.frames = kept;
		item.stats.recovered_frames = kept;

		return true;

	}

	bool open_wal ( pager& item, uint8_t* page ) {

		wal_state& wal = item.wal;

		base::text path;

		path.add ( item.path ).add ( "-wal" );
		base::copy_text ( wal.path, sizeof ( wal.path ), path.value ( ) );
		wal.checkpoint_frames = WAL_CHECKPOINT_FRAMES;

		if ( !base::open_file ( wal.file, wal.path, true ) ) {

			fail ( item, PAGER_IO, "the log file could not be opened", 0, nullptr );

			return false;

		}

		if ( !recover ( item ) )
			return false;

		uint64_t offset = 0;

		if ( !wal_find ( item, 1, offset ) )
			return true;

		if ( !base::read_at ( wal.file, offset + WAL_FRAME_HEADER, page, PAGE_SIZE ) || !page_intact ( page, 1 ) || !decode_header ( page, item.header ) ) {

			fail ( item, PAGER_CORRUPT, "the newest header in the log is damaged", 0, nullptr );

			return false;

		}

		return true;

	}

	static bool ready_batch ( pager& item ) {

		if ( item.wal.batch )
			return true;

		item.wal.batch = base::make < uint8_t > ( static_cast < size_t > ( WAL_BATCH ) * WAL_FRAME );

		if ( !item.wal.batch )
			fail ( item, PAGER_MEMORY, "out of memory while committing", 0, nullptr );

		return item.wal.batch != nullptr;

	}

	bool wal_commit ( pager& item, const uint8_t* header_page ) {

		wal_state& wal = item.wal;

		if ( !ready_batch ( item ) )
			return false;

		uint32_t chain = wal.chain;
		uint32_t written = 0;
		uint32_t pending = 0;
		uint64_t at_offset = wal.end;
		bool fine = true;

		for ( uint32_t i = 0; i <= item.frame_count && fine; i++ ) {

			const uint8_t* data = nullptr;
			uint32_t number = 0;

			if ( i < item.frame_count ) {

				page_frame* frame = item.frames [ i ];

				if ( frame->number <= 1 || !frame->dirty )
					continue;

				seal_page ( frame->data, frame->number );
				data = frame->data;
				number = frame->number;

			} else {

				data = header_page;
				number = 1;

			}

			uint8_t* at = wal.batch + static_cast < size_t > ( pending ) * WAL_FRAME;

			base::write32 ( at, number );
			base::write32 ( at + 4, number == 1 ? item.header.page_count : 0 );
			base::write32 ( at + 8, wal.salt [ 0 ] );
			base::write32 ( at + 12, wal.salt [ 1 ] );
			base::write32 ( at + 20, 0 );
			base::copy ( at + WAL_FRAME_HEADER, data, PAGE_SIZE );
			chain = base::crc32c ( at + WAL_FRAME_HEADER, PAGE_SIZE, base::crc32c ( at, 16, chain ) );
			base::write32 ( at + 16, chain );
			pending++;

			if ( pending == WAL_BATCH || i == item.frame_count ) {

				size_t size = static_cast < size_t > ( pending ) * WAL_FRAME;

				fine = base::write_at ( wal.file, at_offset, wal.batch, size );
				at_offset += size;
				written += pending;
				pending = 0;

			}

		}

		fine = fine && base::sync_file ( wal.file );

		if ( !fine ) {

			fail ( item, PAGER_IO, "the commit could not be written to the log", 0, nullptr );

			return false;

		}

		uint64_t offset = wal.end;

		for ( uint32_t i = 0; i < item.frame_count && fine; i++ ) {

			page_frame* frame = item.frames [ i ];

			if ( frame->number <= 1 || !frame->dirty )
				continue;

			fine = index_put ( wal, frame->number, offset );
			offset += WAL_FRAME;

		}

		fine = fine && index_put ( wal, 1, offset );
		wal.end = at_offset;
		wal.chain = chain;
		wal.frames += written;
		item.stats.writes += written;

		if ( !fine )
			fail ( item, PAGER_MEMORY, "out of memory while indexing the log", 0, nullptr );

		return fine;

	}

	static void sort_entries ( wal_entry* items, uint32_t count ) {

		for ( uint32_t gap = count / 2; gap; gap /= 2 )
			for ( uint32_t i = gap; i < count; i++ ) {

				wal_entry held = items [ i ];
				uint32_t k = i;

				while ( k >= gap && items [ k - gap ].page > held.page ) {

					items [ k ] = items [ k - gap ];
					k -= gap;

				}

				items [ k ] = held;

			}

	}

	bool checkpoint ( pager& item ) {

		wal_state& wal = item.wal;

		if ( !wal.index_used )
			return true;

		wal_entry* entries = base::make < wal_entry > ( wal.index_used + 1 );

		if ( !entries || !ready_batch ( item ) ) {

			base::release ( entries );
			fail ( item, PAGER_MEMORY, "out of memory during a checkpoint", 0, nullptr );

			return false;

		}

		uint32_t count = 0;

		for ( uint32_t i = 0; i < wal.index_room; i++ )
			if ( wal.index [ i ].page && wal.index [ i ].page <= item.committed.page_count )
				entries [ count++ ] = wal.index [ i ];

		sort_entries ( entries, count );

		bool fine = true;
		uint32_t run = 0;
		uint32_t first = 0;

		for ( uint32_t i = 0; i < count && fine; i++ ) {

			const wal_entry& entry = entries [ i ];
			uint8_t* slot = wal.batch + static_cast < size_t > ( run ) * PAGE_SIZE;
			page_frame* frame = cached_frame ( item, entry.page );

			if ( !run )
				first = entry.page;

			if ( frame && !frame->dirty && frame->number != 1 )
				base::copy ( slot, frame->data, PAGE_SIZE );
			else
				fine = base::read_at ( wal.file, entry.offset + WAL_FRAME_HEADER, slot, PAGE_SIZE );

			run++;

			bool last = i + 1 == count || entries [ i + 1 ].page != entry.page + 1 || run == WAL_BATCH;

			if ( fine && last ) {

				fine = base::write_at ( item.file, static_cast < uint64_t > ( first - 1 ) * PAGE_SIZE, wal.batch, static_cast < size_t > ( run ) * PAGE_SIZE );
				run = 0;

			}

		}

		base::release ( entries );
		fine = fine && base::truncate_file ( item.file, static_cast < uint64_t > ( item.committed.page_count ) * PAGE_SIZE ) && base::sync_file ( item.file );

		if ( !fine ) {

			fail ( item, PAGER_IO, "the checkpoint could not write the database file", 0, nullptr );

			return false;

		}

		if ( !fresh_log ( item ) )
			return false;

		item.stats.checkpoints++;

		return true;

	}

	void close_wal ( pager& item ) {

		base::close_file ( item.wal.file );
		base::release ( item.wal.index );
		base::release ( item.wal.batch );
		item.wal.batch = nullptr;
		item.wal.index = nullptr;
		item.wal.index_room = 0;
		item.wal.index_used = 0;

	}

}
