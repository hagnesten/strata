#include "storage/pager/pager.hh"
#include "base/hash/crc32c.hh"
#include "base/platform/os.hh"

namespace storage {

	static uint64_t frame_offset ( uint32_t frame ) {

		return WAL_HEADER + static_cast < uint64_t > ( frame ) * WAL_FRAME;

	}

	static void to_state ( const database_header& header, shared_state_header& out ) {

		out.page_size = header.page_size;
		out.page_count = header.page_count;
		out.free_head = header.free_head;
		out.free_count = header.free_count;
		out.schema_root = header.schema_root;
		out.schema_version = header.schema_version;
		out.change = header.change;

	}

	static void from_state ( const shared_state_header& state, database_header& out ) {

		out.page_size = state.page_size;
		out.page_count = state.page_count;
		out.free_head = state.free_head;
		out.free_count = state.free_count;
		out.schema_root = state.schema_root;
		out.schema_version = state.schema_version;
		out.change = state.change;

	}

	static void encode_wal_header ( const shared_snapshot& snapshot, uint8_t* out, uint32_t& chain ) {

		base::fill ( out, 0, WAL_HEADER );
		base::copy ( out, WAL_MAGIC, 8 );
		base::write32 ( out + 8, 1 );
		base::write32 ( out + 12, PAGE_SIZE );
		base::write32 ( out + 16, snapshot.salt [ 0 ] );
		base::write32 ( out + 20, snapshot.salt [ 1 ] );
		base::write32 ( out + 24, snapshot.wal_sequence );
		base::write32 ( out + 28, base::crc32c ( out, 28 ) );
		chain = base::read32 ( out + 28 );

	}

	static bool fresh_log ( pager& item, shared_snapshot& snapshot, bool flush ) {

		wal_state& wal = item.wal;
		uint8_t header [ WAL_HEADER ];
		uint32_t chain = 0;

		snapshot.salt [ 0 ]++;
		snapshot.salt [ 1 ] = static_cast < uint32_t > ( base::os::ticks ( ) ) ^ snapshot.salt [ 1 ] * 2654435761u ^ base::os::process_id ( );
		snapshot.wal_sequence++;
		encode_wal_header ( snapshot, header, chain );

		bool shrink = base::file_size ( wal.file ) > WAL_KEEP_BYTES;

		if ( !base::write_at ( wal.file, 0, header, WAL_HEADER ) || ( shrink && !base::truncate_file ( wal.file, WAL_HEADER ) ) || ( ( flush || shrink ) && !base::sync_file ( wal.file ) ) ) {

			fail ( item, PAGER_IO, "the log could not be reset", 0, nullptr );

			return false;

		}

		snapshot.chain = chain;
		snapshot.wal_end = WAL_HEADER;
		snapshot.frames = 0;
		snapshot.backfilled = 0;

		return true;

	}

	static bool recover ( pager& item, shared_snapshot& snapshot ) {

		wal_state& wal = item.wal;
		uint8_t header [ WAL_HEADER ];
		uint64_t size = base::file_size ( wal.file );

		if ( size < WAL_HEADER || !base::read_at ( wal.file, 0, header, WAL_HEADER ) || !base::same_bytes ( header, WAL_MAGIC, 8 ) || base::read32 ( header + 8 ) != 1 || base::read32 ( header + 12 ) != PAGE_SIZE || base::read32 ( header + 28 ) != base::crc32c ( header, 28 ) )
			return fresh_log ( item, snapshot, true );

		snapshot.salt [ 0 ] = base::read32 ( header + 16 );
		snapshot.salt [ 1 ] = base::read32 ( header + 20 );
		snapshot.wal_sequence = base::read32 ( header + 24 );
		snapshot.chain = base::read32 ( header + 28 );
		snapshot.wal_end = WAL_HEADER;
		snapshot.frames = 0;

		uint8_t* frame = base::make < uint8_t > ( WAL_FRAME );
		uint32_t possible = static_cast < uint32_t > ( ( size - WAL_HEADER ) / WAL_FRAME );

		if ( !frame || !shared_clear ( wal.shared, possible ) ) {

			base::release ( frame );
			fail ( item, PAGER_MEMORY, "out of memory while reading the log", 0, nullptr );

			return false;

		}

		uint32_t chain = snapshot.chain;
		uint32_t index = 0;
		bool fine = true;

		while ( fine && frame_offset ( index ) + WAL_FRAME <= size && base::read_at ( wal.file, frame_offset ( index ), frame, WAL_FRAME ) ) {

			uint32_t page = base::read32 ( frame );
			uint32_t commit = base::read32 ( frame + 4 );
			uint32_t check = base::crc32c ( frame + WAL_FRAME_HEADER, PAGE_SIZE, base::crc32c ( frame, 16, chain ) );

			if ( base::read32 ( frame + 8 ) != snapshot.salt [ 0 ] || base::read32 ( frame + 12 ) != snapshot.salt [ 1 ] || base::read32 ( frame + 16 ) != check || !page )
				break;

			if ( !shared_append ( wal.shared, index, page ) ) {

				fine = false;

				break;

			}

			chain = check;
			index++;

			if ( commit ) {

				database_header decoded = { };

				if ( page == 1 && page_intact ( frame + WAL_FRAME_HEADER, 1 ) && decode_header ( frame + WAL_FRAME_HEADER, decoded ) )
					to_state ( decoded, snapshot.state );

				snapshot.frames = index;
				snapshot.wal_end = frame_offset ( index );
				snapshot.chain = chain;

			}

		}

		base::release ( frame );

		if ( !fine ) {

			fail ( item, PAGER_MEMORY, "the shared log index is full", 0, nullptr );

			return false;

		}

		item.stats.recovered_frames = snapshot.frames;

		return true;

	}

	bool open_wal ( pager& item, uint8_t* page, bool& recovered ) {

		wal_state& wal = item.wal;
		base::text path;
		bool first = false;

		( void ) page;
		recovered = false;
		path.add ( item.path ).add ( "-wal" );
		base::copy_text ( wal.path, sizeof ( wal.path ), path.value ( ) );
		wal.checkpoint_frames = WAL_CHECKPOINT_FRAMES;
		wal.busy_milliseconds = SHARED_BUSY_MILLISECONDS;

		if ( !base::open_log ( wal.file, wal.path ) ) {

			fail ( item, PAGER_IO, "the log file could not be opened", 0, nullptr );

			return false;

		}

		if ( !open_shared ( wal.shared, item.path, first ) ) {

			fail ( item, PAGER_IO, "the shared log index could not be opened", 0, nullptr );

			return false;

		}

		if ( first ) {

			shared_snapshot snapshot = { };

			snapshot.generation = wal.shared.header->magic == SHARED_MAGIC ? wal.shared.header->generation + 1 : 1;
			snapshot.salt [ 0 ] = wal.shared.header->magic == SHARED_MAGIC ? wal.shared.header->salt [ 0 ] : 0;
			to_state ( item.header, snapshot.state );

			if ( !recover ( item, snapshot ) )
				return false;

			snapshot.backfilled = 0;
			wal.shared.header->checkpointing = 0;
			publish_snapshot ( wal.shared, snapshot );
			share_presence ( wal.shared );

		} else if ( wal.shared.header->magic != SHARED_MAGIC ) {

			fail ( item, PAGER_FORMAT, "the shared log index is not ready", 0, nullptr );

			return false;

		}

		read_snapshot ( wal.shared, wal.snapshot );

		if ( wal.snapshot.frames ) {

			from_state ( wal.snapshot.state, item.header );
			recovered = true;

		}

		return true;

	}

	bool wal_still_valid ( pager& item ) {

		return __atomic_load_n ( &item.wal.shared.header->generation, __ATOMIC_ACQUIRE ) == item.wal.snapshot.generation;

	}

	bool wal_find ( pager& item, uint32_t number, uint64_t& offset ) {

		if ( !item.wal.shared.header )
			return false;

		if ( !item.wal.reading && !begin_read ( item ) )
			return false;

		item.wal.read_any = true;

		uint32_t frame = 0;

		if ( !shared_find ( item.wal.shared, number, item.wal.snapshot.frames, frame ) )
			return false;

		offset = frame_offset ( frame );

		return true;

	}

	static void forget_cached ( pager& item, const shared_snapshot& latest ) {

		const shared_snapshot& mine = item.wal.snapshot;

		if ( latest.generation == mine.generation && latest.frames == mine.frames )
			return;

		bool everything = latest.generation != mine.generation || latest.frames < mine.frames || latest.frames - mine.frames > item.frame_count;

		if ( !everything ) {

			for ( uint32_t f = mine.frames; f < latest.frames; f++ ) {

				shared_segment* segment = nullptr;

				if ( !shared_segment_at ( item.wal.shared, f / SHARED_SEGMENT_FRAMES, segment ) ) {

					everything = true;

					break;

				}

				uint32_t number = segment->pages [ f % SHARED_SEGMENT_FRAMES ];

				for ( page_frame* frame = item.buckets [ number * 2654435761u >> 20 & ( PAGER_BUCKETS - 1 ) ]; frame; frame = frame->chain ) {

					if ( frame->number == number && !frame->dirty && !frame->pins ) {

						discard_frame ( item, frame );

						break;

					}

				}

			}

		}

		if ( everything )
			for ( uint32_t i = 0; i < item.frame_count; i++ )
				if ( item.frames [ i ]->number && !item.frames [ i ]->dirty && !item.frames [ i ]->pins )
					discard_frame ( item, item.frames [ i ] );

		item.shape++;
		item.layout++;

	}

	static void adopt ( pager& item, const shared_snapshot& latest ) {

		forget_cached ( item, latest );
		item.wal.snapshot = latest;
		from_state ( latest.state, item.header );
		item.committed = item.header;

	}

	bool begin_read ( pager& item ) {

		if ( item.wal.reading || !item.wal.shared.header )
			return true;

		shared_snapshot latest;

		register_reader ( item.wal.shared, latest );

		if ( !item.wal.writing )
			adopt ( item, latest );

		item.wal.reading = true;
		item.wal.read_any = false;

		return true;

	}

	void end_read ( pager& item ) {

		if ( !item.wal.reading || item.wal.writing )
			return;

		release_reader ( item.wal.shared );
		item.wal.reading = false;
		item.wal.read_any = false;

	}

	bool begin_write ( pager& item ) {

		wal_state& wal = item.wal;

		if ( wal.writing || !wal.shared.header )
			return true;

		if ( !wal.reading ) {

			if ( !take_write_lock ( wal.shared, wal.busy_milliseconds ) ) {

				fail ( item, PAGER_BUSY, "the database is busy, another connection is writing", 0, nullptr );

				return false;

			}

			begin_read ( item );
			wal.writing = true;

			return true;

		}

		if ( !take_write_lock ( wal.shared, wal.busy_milliseconds ) ) {

			fail ( item, PAGER_BUSY, "the database is busy, another connection is writing", 0, nullptr );

			return false;

		}

		shared_snapshot latest;

		read_snapshot ( wal.shared, latest );

		if ( latest.generation != wal.snapshot.generation || latest.frames != wal.snapshot.frames ) {

			if ( wal.read_any || has_changes ( item ) ) {

				drop_write_lock ( wal.shared );
				fail ( item, PAGER_BUSY, "the database changed after this transaction read it, start the transaction again", 0, nullptr );

				return false;

			}

			adopt ( item, latest );
			__atomic_store_n ( &wal.shared.header->reader_generations [ wal.shared.slot ], latest.generation, __ATOMIC_RELAXED );
			__atomic_store_n ( &wal.shared.header->readers [ wal.shared.slot ], latest.frames, __ATOMIC_RELEASE );

		}

		wal.writing = true;

		return true;

	}

	void end_write ( pager& item ) {

		if ( !item.wal.writing )
			return;

		drop_write_lock ( item.wal.shared );
		item.wal.writing = false;

	}

	bool pager_alone ( pager& item ) {

		return !item.wal.shared.header || alone ( item.wal.shared );

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
		shared_snapshot& snapshot = wal.snapshot;

		if ( !begin_write ( item ) || !ready_batch ( item ) )
			return false;

		uint32_t chain = snapshot.chain;
		uint32_t written = 0;
		uint32_t pending = 0;
		uint64_t at_offset = snapshot.wal_end;
		bool fine = true;
		uint64_t needed = at_offset + static_cast < uint64_t > ( item.frame_count + 1 ) * WAL_FRAME;

		if ( base::file_size ( wal.file ) < needed ) {

			uint64_t grown = ( needed + WAL_GROW_BYTES - 1 ) / WAL_GROW_BYTES * WAL_GROW_BYTES;

			if ( !base::truncate_file ( wal.file, grown ) || !base::sync_file ( wal.file ) ) {

				fail ( item, PAGER_IO, "the log could not grow", 0, nullptr );

				return false;

			}

		}

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
			base::write32 ( at + 8, snapshot.salt [ 0 ] );
			base::write32 ( at + 12, snapshot.salt [ 1 ] );
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

		if ( !fine ) {

			fail ( item, PAGER_IO, "the commit could not be written to the log", 0, nullptr );

			return false;

		}

		uint32_t next = snapshot.frames;

		for ( uint32_t i = 0; i < item.frame_count && fine; i++ ) {

			page_frame* frame = item.frames [ i ];

			if ( frame->number <= 1 || !frame->dirty )
				continue;

			fine = shared_append ( wal.shared, next++, frame->number );

		}

		fine = fine && shared_append ( wal.shared, next++, 1 );

		if ( !fine ) {

			item.broken = true;
			fail ( item, PAGER_MEMORY, "the shared log index is full, reopen the database", 0, nullptr );

			return false;

		}

		snapshot.frames = next;
		snapshot.wal_end = at_offset;
		snapshot.chain = chain;
		to_state ( item.header, snapshot.state );
		publish_snapshot ( wal.shared, snapshot );

		if ( wal.reading ) {

			__atomic_store_n ( &wal.shared.header->reader_generations [ wal.shared.slot ], snapshot.generation, __ATOMIC_RELAXED );
			__atomic_store_n ( &wal.shared.header->readers [ wal.shared.slot ], snapshot.frames, __ATOMIC_RELEASE );

		}

		item.stats.writes += written;

		return true;

	}

	static void sort_entries ( wal_entry* items, uint32_t count ) {

		for ( uint32_t gap = count / 2; gap; gap /= 2 )
			for ( uint32_t i = gap; i < count; i++ ) {

				wal_entry held = items [ i ];
				uint32_t k = i;

				while ( k >= gap && ( items [ k - gap ].page > held.page || ( items [ k - gap ].page == held.page && items [ k - gap ].frame > held.frame ) ) ) {

					items [ k ] = items [ k - gap ];
					k -= gap;

				}

				items [ k ] = held;

			}

	}

	static bool backfill ( pager& item, uint32_t from, uint32_t to, bool latest ) {

		wal_state& wal = item.wal;
		uint32_t count = to - from;
		wal_entry* entries = base::make < wal_entry > ( count + 1 );

		if ( !entries || !ready_batch ( item ) ) {

			base::release ( entries );
			fail ( item, PAGER_MEMORY, "out of memory during a checkpoint", 0, nullptr );

			return false;

		}

		for ( uint32_t f = from; f < to; f++ ) {

			shared_segment* segment = nullptr;

			if ( !shared_segment_at ( wal.shared, f / SHARED_SEGMENT_FRAMES, segment ) ) {

				base::release ( entries );
				fail ( item, PAGER_IO, "the shared log index could not be read", 0, nullptr );

				return false;

			}

			entries [ f - from ] = { segment->pages [ f % SHARED_SEGMENT_FRAMES ], f };

		}

		sort_entries ( entries, count );

		uint32_t kept = 0;

		for ( uint32_t i = 0; i < count; i++ )
			if ( i + 1 == count || entries [ i + 1 ].page != entries [ i ].page )
				entries [ kept++ ] = entries [ i ];

		bool fine = true;
		uint32_t run = 0;
		uint32_t first = 0;

		for ( uint32_t i = 0; i < kept && fine; i++ ) {

			const wal_entry& entry = entries [ i ];

			if ( !entry.page || entry.page > item.committed.page_count )
				continue;

			uint8_t* slot = wal.batch + static_cast < size_t > ( run ) * PAGE_SIZE;
			page_frame* frame = latest ? cached_frame ( item, entry.page ) : nullptr;

			if ( !run )
				first = entry.page;

			if ( frame && !frame->dirty && frame->number != 1 )
				base::copy ( slot, frame->data, PAGE_SIZE );
			else
				fine = base::read_at ( wal.file, frame_offset ( entry.frame ) + WAL_FRAME_HEADER, slot, PAGE_SIZE );

			run++;

			bool last = i + 1 == kept || entries [ i + 1 ].page != entry.page + 1 || run == WAL_BATCH;

			if ( fine && last ) {

				fine = base::write_at ( item.file, static_cast < uint64_t > ( first - 1 ) * PAGE_SIZE, wal.batch, static_cast < size_t > ( run ) * PAGE_SIZE );
				run = 0;

			}

		}

		base::release ( entries );

		uint64_t wanted = static_cast < uint64_t > ( item.committed.page_count ) * PAGE_SIZE;

		fine = fine && ( base::file_size ( item.file ) >= wanted || base::truncate_file ( item.file, wanted ) ) && base::sync_file ( item.file );

		if ( !fine )
			fail ( item, PAGER_IO, "the checkpoint could not write the database file", 0, nullptr );

		return fine;

	}

	bool checkpoint ( pager& item ) {

		wal_state& wal = item.wal;
		shared_snapshot& snapshot = wal.snapshot;

		if ( !wal.shared.header || !wal.writing || !snapshot.frames )
			return true;

		if ( !base::os::lock_range ( wal.shared.file, SHARED_LOCK_CHECKPOINT, 1, true, false ) )
			return true;

		begin_checkpointing ( wal.shared );

		bool others = false;
		uint32_t oldest = oldest_reader ( wal.shared, snapshot.generation, snapshot.frames, others );
		uint32_t target = oldest < snapshot.frames ? oldest : snapshot.frames;
		bool fine = true;

		if ( target > snapshot.backfilled ) {

			fine = backfill ( item, snapshot.backfilled, target, target == snapshot.frames );

			if ( fine ) {

				snapshot.backfilled = target;
				publish_snapshot ( wal.shared, snapshot );

			}

		}

		if ( fine && snapshot.backfilled == snapshot.frames && oldest >= snapshot.frames ) {

			uint32_t used = snapshot.frames;

			snapshot.generation++;
			fine = fresh_log ( item, snapshot, false ) && shared_clear ( wal.shared, used );

			if ( fine ) {

				publish_snapshot ( wal.shared, snapshot );

				if ( wal.reading ) {

					__atomic_store_n ( &wal.shared.header->reader_generations [ wal.shared.slot ], snapshot.generation, __ATOMIC_RELAXED );
					__atomic_store_n ( &wal.shared.header->readers [ wal.shared.slot ], snapshot.frames, __ATOMIC_RELEASE );

				}

				item.stats.checkpoints++;

			}

		}

		end_checkpointing ( wal.shared );
		base::os::unlock_range ( wal.shared.file, SHARED_LOCK_CHECKPOINT, 1 );

		return fine;

	}

	void close_wal ( pager& item ) {

		end_write ( item );

		if ( item.wal.reading )
			release_reader ( item.wal.shared );

		item.wal.reading = false;
		close_shared ( item.wal.shared );
		base::close_file ( item.wal.file );
		base::release ( item.wal.batch );
		item.wal.batch = nullptr;

	}

}
