#include "storage/shared/shared.hh"
#include "base/platform/os.hh"
#include "base/runtime/memory.hh"
#include "base/text/text.hh"

namespace storage {

	static uint32_t slot_of ( uint32_t page ) {

		return page * 2654435761u & ( SHARED_HASH_SLOTS - 1 );

	}

	bool open_shared ( shared_view& out, const char* path, bool& first ) {

		base::text name;

		base::fill ( &out, 0, sizeof ( out ) );
		out.slot = -1;
		first = false;
		name.add ( path ).add ( "-shm" );

		if ( !base::os::open_file ( out.file, name.value ( ), true, true ) )
			return false;

		out.header = static_cast < shared_header* > ( base::os::map_file ( out.file, 0, SHARED_HEADER_BYTES, out.header_mapping ) );

		if ( !out.header ) {

			base::os::close_file ( out.file );
			out.file = nullptr;

			return false;

		}

		if ( base::os::lock_range ( out.file, SHARED_LOCK_PRESENT, 1, true, false ) )
			first = true;
		else if ( !base::os::lock_range ( out.file, SHARED_LOCK_PRESENT, 1, false, true ) ) {

			close_shared ( out );

			return false;

		}

		out.present = true;

		for ( int32_t s = 0; s < SHARED_READERS; s++ ) {

			if ( base::os::lock_range ( out.file, SHARED_LOCK_READER + static_cast < uint64_t > ( s ), 1, true, false ) ) {

				out.slot = s;

				break;

			}

		}

		if ( out.slot < 0 ) {

			close_shared ( out );

			return false;

		}

		out.header->readers [ out.slot ] = SHARED_IDLE;

		return true;

	}

	void share_presence ( shared_view& item ) {

		base::os::unlock_range ( item.file, SHARED_LOCK_PRESENT, 1 );
		base::os::lock_range ( item.file, SHARED_LOCK_PRESENT, 1, false, true );

	}

	void close_shared ( shared_view& item ) {

		if ( item.header && item.slot >= 0 )
			item.header->readers [ item.slot ] = SHARED_IDLE;

		for ( uint32_t i = 0; i < SHARED_SEGMENTS; i++ )
			if ( item.segments [ i ] )
				base::os::unmap_file ( item.segments [ i ], SHARED_SEGMENT_BYTES, item.segment_mappings [ i ] );

		if ( item.header )
			base::os::unmap_file ( item.header, SHARED_HEADER_BYTES, item.header_mapping );

		if ( item.file ) {

			if ( item.slot >= 0 )
				base::os::unlock_range ( item.file, SHARED_LOCK_READER + static_cast < uint64_t > ( item.slot ), 1 );

			if ( item.present )
				base::os::unlock_range ( item.file, SHARED_LOCK_PRESENT, 1 );

			base::os::close_file ( item.file );

		}

		base::fill ( &item, 0, sizeof ( item ) );
		item.slot = -1;

	}

	bool shared_segment_at ( shared_view& item, uint32_t index, shared_segment*& out ) {

		if ( index >= SHARED_SEGMENTS )
			return false;

		if ( !item.segments [ index ] ) {

			uint64_t offset = SHARED_HEADER_BYTES + static_cast < uint64_t > ( index ) * SHARED_SEGMENT_BYTES;

			item.segments [ index ] = static_cast < shared_segment* > ( base::os::map_file ( item.file, offset, SHARED_SEGMENT_BYTES, item.segment_mappings [ index ] ) );

			if ( !item.segments [ index ] )
				return false;

		}

		out = item.segments [ index ];

		return true;

	}

	void read_snapshot ( const shared_view& item, shared_snapshot& out ) {

		const shared_header* header = item.header;

		for ( ;; ) {

			uint32_t before = __atomic_load_n ( &header->sequence, __ATOMIC_ACQUIRE );

			if ( before & 1 ) {

				__builtin_ia32_pause ( );

				continue;

			}

			out.generation = header->generation;
			out.frames = header->frames;
			out.backfilled = header->backfilled;
			out.salt [ 0 ] = header->salt [ 0 ];
			out.salt [ 1 ] = header->salt [ 1 ];
			out.chain = header->chain;
			out.wal_sequence = header->wal_sequence;
			out.wal_end = header->wal_end;
			out.state = header->state;
			__atomic_thread_fence ( __ATOMIC_ACQUIRE );

			if ( __atomic_load_n ( &header->sequence, __ATOMIC_RELAXED ) == before )
				return;

		}

	}

	void publish_snapshot ( shared_view& item, const shared_snapshot& value ) {

		shared_header* header = item.header;
		uint32_t sequence = header->sequence;

		__atomic_store_n ( &header->sequence, sequence + 1, __ATOMIC_RELEASE );
		__atomic_thread_fence ( __ATOMIC_SEQ_CST );
		header->magic = SHARED_MAGIC;
		header->version = SHARED_VERSION;
		header->generation = value.generation;
		header->frames = value.frames;
		header->backfilled = value.backfilled;
		header->salt [ 0 ] = value.salt [ 0 ];
		header->salt [ 1 ] = value.salt [ 1 ];
		header->chain = value.chain;
		header->wal_sequence = value.wal_sequence;
		header->wal_end = value.wal_end;
		header->state = value.state;
		__atomic_store_n ( &header->sequence, sequence + 2, __ATOMIC_RELEASE );

	}

	bool shared_find ( shared_view& item, uint32_t page, uint32_t limit, uint32_t& frame ) {

		if ( !limit )
			return false;

		for ( int32_t s = static_cast < int32_t > ( ( limit - 1 ) / SHARED_SEGMENT_FRAMES ); s >= 0; s-- ) {

			shared_segment* segment = nullptr;

			if ( !shared_segment_at ( item, static_cast < uint32_t > ( s ), segment ) )
				return false;

			uint32_t at = slot_of ( page );

			for ( uint32_t probe = 0; probe < SHARED_HASH_SLOTS; probe++ ) {

				uint32_t value = __atomic_load_n ( &segment->hash [ at ], __ATOMIC_ACQUIRE );

				if ( !value )
					break;

				uint32_t local = value - 1;

				if ( local < SHARED_SEGMENT_FRAMES && segment->pages [ local ] == page ) {

					for ( uint32_t guard = 0; guard < SHARED_SEGMENT_FRAMES; guard++ ) {

						uint32_t global = static_cast < uint32_t > ( s ) * SHARED_SEGMENT_FRAMES + local;

						if ( global < limit ) {

							frame = global;

							return true;

						}

						uint32_t previous = segment->previous [ local ];

						if ( !previous || previous - 1 >= SHARED_SEGMENT_FRAMES )
							break;

						local = previous - 1;

					}

					break;

				}

				at = ( at + 1 ) & ( SHARED_HASH_SLOTS - 1 );

			}

		}

		return false;

	}

	bool shared_append ( shared_view& item, uint32_t frame, uint32_t page ) {

		shared_segment* segment = nullptr;

		if ( !shared_segment_at ( item, frame / SHARED_SEGMENT_FRAMES, segment ) )
			return false;

		uint32_t local = frame % SHARED_SEGMENT_FRAMES;
		uint32_t at = slot_of ( page );

		segment->pages [ local ] = page;

		for ( uint32_t probe = 0; probe < SHARED_HASH_SLOTS; probe++ ) {

			uint32_t value = segment->hash [ at ];

			if ( !value || ( value - 1 < SHARED_SEGMENT_FRAMES && segment->pages [ value - 1 ] == page ) ) {

				segment->previous [ local ] = value;
				__atomic_store_n ( &segment->hash [ at ], local + 1, __ATOMIC_RELEASE );

				return true;

			}

			at = ( at + 1 ) & ( SHARED_HASH_SLOTS - 1 );

		}

		return false;

	}

	bool shared_clear ( shared_view& item, uint32_t frames ) {

		uint32_t used = frames ? ( frames - 1 ) / SHARED_SEGMENT_FRAMES + 1 : 1;

		for ( uint32_t s = 0; s < used && s < SHARED_SEGMENTS; s++ ) {

			shared_segment* segment = nullptr;

			if ( !shared_segment_at ( item, s, segment ) )
				return false;

			for ( uint32_t h = 0; h < SHARED_HASH_SLOTS; h++ )
				__atomic_store_n ( &segment->hash [ h ], 0u, __ATOMIC_RELAXED );

		}

		__atomic_thread_fence ( __ATOMIC_SEQ_CST );

		return true;

	}

	void register_reader ( shared_view& item, shared_snapshot& out ) {

		shared_header* header = item.header;

		for ( ;; ) {

			read_snapshot ( item, out );
			__atomic_store_n ( &header->reader_generations [ item.slot ], out.generation, __ATOMIC_RELAXED );
			__atomic_store_n ( &header->readers [ item.slot ], out.frames, __ATOMIC_RELAXED );
			__atomic_thread_fence ( __ATOMIC_SEQ_CST );

			if ( __atomic_load_n ( &header->checkpointing, __ATOMIC_ACQUIRE ) ) {

				__atomic_store_n ( &header->readers [ item.slot ], SHARED_IDLE, __ATOMIC_RELEASE );
				base::os::sleep_milliseconds ( 0 );

				continue;

			}

			shared_snapshot again;

			read_snapshot ( item, again );

			if ( again.frames == out.frames && again.generation == out.generation ) {

				out = again;

				return;

			}

		}

	}

	void release_reader ( shared_view& item ) {

		__atomic_store_n ( &item.header->readers [ item.slot ], SHARED_IDLE, __ATOMIC_RELEASE );

	}

	void begin_checkpointing ( shared_view& item ) {

		__atomic_store_n ( &item.header->checkpointing, 1u, __ATOMIC_RELAXED );
		__atomic_thread_fence ( __ATOMIC_SEQ_CST );

	}

	void end_checkpointing ( shared_view& item ) {

		__atomic_store_n ( &item.header->checkpointing, 0u, __ATOMIC_RELEASE );

	}

	uint32_t oldest_reader ( shared_view& item, uint32_t generation, uint32_t frames, bool& others ) {

		uint32_t oldest = frames;

		others = false;

		for ( int32_t s = 0; s < SHARED_READERS; s++ ) {

			if ( s == item.slot )
				continue;

			if ( base::os::lock_range ( item.file, SHARED_LOCK_READER + static_cast < uint64_t > ( s ), 1, true, false ) ) {

				base::os::unlock_range ( item.file, SHARED_LOCK_READER + static_cast < uint64_t > ( s ), 1 );

				continue;

			}

			others = true;

			uint32_t value = __atomic_load_n ( &item.header->readers [ s ], __ATOMIC_ACQUIRE );

			if ( value == SHARED_IDLE )
				continue;

			if ( __atomic_load_n ( &item.header->reader_generations [ s ], __ATOMIC_ACQUIRE ) != generation )
				return 0;

			if ( value < oldest )
				oldest = value;

		}

		return oldest;

	}

	bool take_write_lock ( shared_view& item, uint32_t milliseconds ) {

		for ( uint32_t waited = 0; ; waited++ ) {

			if ( base::os::lock_range ( item.file, SHARED_LOCK_WRITE, 1, true, false ) )
				return true;

			if ( waited >= milliseconds )
				return false;

			base::os::sleep_milliseconds ( 1 );

		}

	}

	void drop_write_lock ( shared_view& item ) {

		base::os::unlock_range ( item.file, SHARED_LOCK_WRITE, 1 );

	}

	bool alone ( shared_view& item ) {

		if ( !item.present )
			return true;

		base::os::unlock_range ( item.file, SHARED_LOCK_PRESENT, 1 );

		bool only = base::os::lock_range ( item.file, SHARED_LOCK_PRESENT, 1, true, false );

		if ( only )
			base::os::unlock_range ( item.file, SHARED_LOCK_PRESENT, 1 );

		base::os::lock_range ( item.file, SHARED_LOCK_PRESENT, 1, false, true );

		return only;

	}

}
