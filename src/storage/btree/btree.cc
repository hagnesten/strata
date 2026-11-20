#include "storage/btree/btree.hh"

namespace storage {

	static uint32_t count_of ( const uint8_t* page ) {

		return base::read16 ( page + 2 );

	}

	static uint32_t link_a ( const uint8_t* page ) {

		return base::read32 ( page + 8 );

	}

	static uint32_t link_b ( const uint8_t* page ) {

		return base::read32 ( page + 12 );

	}

	static uint32_t offset_of ( const uint8_t* page, uint32_t slot ) {

		return base::read16 ( page + BTREE_HEADER + slot * 2 );

	}

	static bool leaf_kind ( uint8_t kind ) {

		return kind == PAGE_TABLE_LEAF || kind == PAGE_INDEX_LEAF;

	}

	static bool tree_kind ( uint8_t kind ) {

		return kind >= PAGE_TABLE_LEAF && kind <= PAGE_INDEX_INTERIOR;

	}

	static bool index_kind ( uint8_t kind ) {

		return kind == PAGE_INDEX_LEAF || kind == PAGE_INDEX_INTERIOR;

	}

	static uint32_t local_size ( uint64_t total ) {

		return total <= BTREE_MAX_LOCAL ? static_cast < uint32_t > ( total ) : BTREE_MIN_LOCAL;

	}

	static void corrupt ( pager& owner, const char* what, uint32_t number ) {

		base::text line;

		line.add ( what ).add ( " on page " ).number ( number );
		owner.error = PAGER_CORRUPT;
		base::copy_text ( owner.problem, sizeof ( owner.problem ), line.value ( ) );

	}

	static bool parse_body ( const uint8_t* data, size_t size, size_t at, bool index, bool leaf, cell_info& out ) {

		uint64_t number = 0;

		if ( !index ) {

			if ( !base::read_varint ( data, size, at, number ) )
				return false;

			out.rowid = base::unzigzag ( number );

			if ( !leaf ) {

				out.size = static_cast < uint32_t > ( at );

				return true;

			}

		}

		if ( !base::read_varint ( data, size, at, out.payload ) )
			return false;

		out.local = local_size ( out.payload );

		if ( out.local > size - at )
			return false;

		out.local_data = data + at;
		at += out.local;

		if ( out.payload > out.local ) {

			if ( size - at < 4 )
				return false;

			out.overflow = base::read32 ( data + at );
			at += 4;

		}

		out.size = static_cast < uint32_t > ( at );

		return true;

	}

	static bool parse_cell ( const uint8_t* page, uint32_t slot, cell_info& out ) {

		uint8_t kind = page [ 0 ];
		bool leaf = leaf_kind ( kind );
		uint32_t offset = offset_of ( page, slot );

		out = { };

		if ( offset < BTREE_HEADER + count_of ( page ) * 2 || offset >= PAGE_USABLE )
			return false;

		const uint8_t* data = page + offset;
		size_t size = PAGE_USABLE - offset;
		size_t at = 0;

		if ( !leaf ) {

			if ( size < 4 )
				return false;

			out.child = base::read32 ( data );
			at = 4;

		}

		return parse_body ( data, size, at, index_kind ( kind ), leaf, out );

	}

	static uint32_t used_bytes ( const uint8_t* page ) {

		uint32_t used = 0;

		for ( uint32_t slot = 0; slot < count_of ( page ); slot++ ) {

			cell_info info;

			if ( parse_cell ( page, slot, info ) )
				used += info.size + 2;

		}

		return used;

	}

	static uint32_t gaps_of ( const uint8_t* page ) {

		return base::read16 ( page + 6 );

	}

	static bool content_sane ( const uint8_t* page, uint32_t count, uint32_t& content ) {

		content = base::read16 ( page + 4 );

		return content && content <= PAGE_USABLE && content >= BTREE_HEADER + count * 2;

	}

	static uint32_t quick_used ( const uint8_t* page ) {

		uint32_t count = count_of ( page );
		uint32_t content = 0;

		if ( !content_sane ( page, count, content ) || PAGE_USABLE - content < gaps_of ( page ) )
			return used_bytes ( page );

		return PAGE_USABLE - content - gaps_of ( page ) + count * 2;

	}

	static bool place_cell ( uint8_t* page, uint32_t slot, const uint8_t* cell, uint32_t size ) {

		uint32_t count = count_of ( page );
		uint32_t content = 0;

		if ( count >= BTREE_CELLS || slot > count || !content_sane ( page, count, content ) || content - ( BTREE_HEADER + count * 2 ) < size + 2 )
			return false;

		uint8_t* slots = page + BTREE_HEADER;

		content -= size;
		base::copy ( page + content, cell, size );

		if ( slot < count )
			base::move ( slots + slot * 2 + 2, slots + slot * 2, ( count - slot ) * 2 );

		base::write16 ( slots + slot * 2, content );
		base::write16 ( page + 2, count + 1 );
		base::write16 ( page + 4, content );

		return true;

	}

	static bool swap_cell ( uint8_t* page, uint32_t slot, uint32_t old_size, const uint8_t* cell, uint32_t size ) {

		uint32_t offset = offset_of ( page, slot );
		uint32_t gaps = gaps_of ( page );

		if ( size <= old_size ) {

			base::copy ( page + offset, cell, size );
			base::write16 ( page + 6, gaps + old_size - size );

			return true;

		}

		uint32_t count = count_of ( page );
		uint32_t content = 0;

		if ( !content_sane ( page, count, content ) || content - ( BTREE_HEADER + count * 2 ) < size )
			return false;

		content -= size;
		base::copy ( page + content, cell, size );
		base::write16 ( page + BTREE_HEADER + slot * 2, content );
		base::write16 ( page + 4, content );
		base::write16 ( page + 6, gaps + old_size );

		return true;

	}

	static void drop_cell ( uint8_t* page, uint32_t slot, uint32_t size ) {

		uint32_t count = count_of ( page );
		uint32_t offset = offset_of ( page, slot );
		uint8_t* slots = page + BTREE_HEADER;

		if ( slot + 1 < count )
			base::move ( slots + slot * 2, slots + slot * 2 + 2, ( count - slot - 1 ) * 2 );

		base::write16 ( slots + ( count - 1 ) * 2, 0 );
		base::write16 ( page + 2, count - 1 );

		if ( offset == base::read16 ( page + 4 ) )
			base::write16 ( page + 4, offset + size );
		else
			base::write16 ( page + 6, gaps_of ( page ) + size );

	}

	static bool read_full ( pager& owner, const cell_info& info, base::text& out ) {

		out.clear ( );
		out.add ( reinterpret_cast < const char* > ( info.local_data ), info.local );

		uint64_t left = info.payload - info.local;
		uint32_t number = info.overflow;
		uint32_t guard = 0;

		while ( left ) {

			if ( !number || ++guard > owner.header.page_count ) {

				corrupt ( owner, "an overflow chain ends early", number );

				return false;

			}

			page_frame* frame = get_page ( owner, number );

			if ( !frame )
				return false;

			uint32_t used = base::read16 ( frame->data + 8 );

			if ( frame->data [ 0 ] != PAGE_OVERFLOW || used > OVERFLOW_ROOM || !used ) {

				release_page ( frame );
				corrupt ( owner, "a broken overflow page", number );

				return false;

			}

			uint32_t take = static_cast < uint32_t > ( base::min < uint64_t > ( used, left ) );

			out.add ( reinterpret_cast < const char* > ( frame->data + OVERFLOW_HEADER ), take );
			left -= take;
			number = base::read32 ( frame->data + 4 );
			release_page ( frame );

		}

		return true;

	}

	static bool write_overflow ( pager& owner, const uint8_t* data, size_t size, uint32_t& first ) {

		page_frame* previous = nullptr;

		first = 0;

		while ( size ) {

			page_frame* frame = new_page ( owner, PAGE_OVERFLOW );

			if ( !frame ) {

				release_page ( previous );

				return false;

			}

			uint32_t take = static_cast < uint32_t > ( base::min < size_t > ( size, OVERFLOW_ROOM ) );

			base::write16 ( frame->data + 8, take );
			base::copy ( frame->data + OVERFLOW_HEADER, data, take );

			if ( previous ) {

				base::write32 ( previous->data + 4, frame->number );
				release_page ( previous );

			} else
				first = frame->number;

			previous = frame;
			data += take;
			size -= take;

		}

		release_page ( previous );

		return true;

	}

	static bool free_overflow ( pager& owner, uint32_t number ) {

		uint32_t guard = 0;

		while ( number ) {

			if ( ++guard > owner.header.page_count ) {

				corrupt ( owner, "an overflow chain loops", number );

				return false;

			}

			page_frame* frame = get_page ( owner, number );

			if ( !frame )
				return false;

			uint32_t next = base::read32 ( frame->data + 4 );
			bool overflow = frame->data [ 0 ] == PAGE_OVERFLOW;

			release_page ( frame );

			if ( !overflow ) {

				corrupt ( owner, "an overflow chain points at a non-overflow page", number );

				return false;

			}

			if ( !free_page ( owner, number ) )
				return false;

			number = next;

		}

		return true;

	}

	static void add_varint ( base::text& out, uint64_t number ) {

		uint8_t bytes [ 10 ];

		out.add ( reinterpret_cast < const char* > ( bytes ), base::write_varint ( bytes, number ) );

	}

	static void add_u32 ( base::text& out, uint32_t number ) {

		uint8_t bytes [ 4 ];

		base::write32 ( bytes, number );
		out.add ( reinterpret_cast < const char* > ( bytes ), 4 );

	}

	static bool payload_body ( pager& owner, const uint8_t* data, size_t size, base::text& out ) {

		uint32_t local = local_size ( size );

		add_varint ( out, size );
		out.add ( reinterpret_cast < const char* > ( data ), local );

		if ( size > local ) {

			uint32_t first = 0;

			if ( !write_overflow ( owner, data + local, size - local, first ) )
				return false;

			add_u32 ( out, first );

		}

		return true;

	}

	static bool rebuild ( uint8_t* page, uint8_t kind, const cell_span* cells, uint32_t count, uint32_t a, uint32_t b ) {

		uint32_t total = 0;

		for ( uint32_t i = 0; i < count; i++ )
			total += cells [ i ].size + 2;

		if ( total > BTREE_SPACE || count > BTREE_CELLS )
			return false;

		base::fill ( page, 0, PAGE_USABLE );
		page [ 0 ] = kind;
		base::write16 ( page + 2, count );
		base::write32 ( page + 8, a );
		base::write32 ( page + 12, b );

		uint32_t content = PAGE_USABLE;

		for ( uint32_t i = 0; i < count; i++ ) {

			content -= cells [ i ].size;
			base::copy ( page + content, cells [ i ].data, cells [ i ].size );
			base::write16 ( page + BTREE_HEADER + i * 2, content );

		}

		base::write16 ( page + 4, content );

		return true;

	}

	static bool borrow_space ( pager& owner, uint8_t*& scratch, cell_span*& cells ) {

		if ( owner.spare_scratch && owner.spare_cells ) {

			scratch = owner.spare_scratch;
			cells = static_cast < cell_span* > ( owner.spare_cells );
			owner.spare_scratch = nullptr;
			owner.spare_cells = nullptr;

			return true;

		}

		scratch = static_cast < uint8_t* > ( base::allocate ( PAGE_SIZE * 2 ) );
		cells = static_cast < cell_span* > ( base::allocate ( sizeof ( cell_span ) * ( BTREE_CELLS + 2 ) ) );

		return scratch && cells;

	}

	static void return_space ( pager& owner, uint8_t* scratch, cell_span* cells ) {

		if ( scratch && cells && !owner.spare_scratch && !owner.spare_cells ) {

			owner.spare_scratch = scratch;
			owner.spare_cells = cells;

			return;

		}

		base::release ( scratch );
		base::release ( cells );

	}

	bool fill_page ( uint8_t* page, uint8_t kind, const cell_span* cells, uint32_t count, uint32_t a, uint32_t b ) {

		return rebuild ( page, kind, cells, count, a, b );

	}

	bool key_body ( pager& owner, const uint8_t* data, size_t size, base::text& out ) {

		return payload_body ( owner, data, size, out );

	}

	static bool gather ( pager& owner, const page_frame* frame, uint8_t* scratch, uint32_t& used, cell_span* cells, uint32_t& count ) {

		uint32_t total = count_of ( frame->data );

		if ( count + total > BTREE_CELLS ) {

			corrupt ( owner, "too many cells", frame->number );

			return false;

		}

		for ( uint32_t slot = 0; slot < total; slot++ ) {

			cell_info info;

			if ( !parse_cell ( frame->data, slot, info ) ) {

				corrupt ( owner, "a cell runs past its page", frame->number );

				return false;

			}

			base::copy ( scratch + used, frame->data + offset_of ( frame->data, slot ), info.size );
			cells [ count++ ] = { scratch + used, info.size };
			used += info.size;

		}

		return true;

	}

	static bool parse_span ( const cell_span& span, uint8_t kind, cell_info& out ) {

		bool leaf = leaf_kind ( kind );

		out = { };

		if ( !leaf ) {

			if ( span.size < 4 )
				return false;

			out.child = base::read32 ( span.data );

		}

		return parse_body ( span.data, span.size, leaf ? 0 : 4, index_kind ( kind ), leaf, out );

	}

	static int32_t index_order ( pager& owner, const uint8_t* page, uint32_t slot, uint32_t count, const key_probe& probe, bool& fine ) {

		uint32_t offset = offset_of ( page, slot );

		fine = false;

		if ( offset < BTREE_HEADER + count * 2 || offset >= PAGE_USABLE )
			return 0;

		const uint8_t* data = page + offset;
		size_t size = PAGE_USABLE - offset;
		size_t at = leaf_kind ( page [ 0 ] ) ? 0 : 4;
		uint64_t payload = 0;

		if ( !base::read_varint ( data, size, at, payload ) )
			return 0;

		if ( payload <= BTREE_MAX_LOCAL ) {

			if ( payload > size - at )
				return 0;

			fine = true;

			return compare_probe ( data + at, static_cast < size_t > ( payload ), probe );

		}

		cell_info info;
		base::text whole;

		if ( !parse_cell ( page, slot, info ) || !read_full ( owner, info, whole ) )
			return 0;

		fine = true;

		return compare_probe ( reinterpret_cast < const uint8_t* > ( whole.value ( ) ), whole.size, probe );

	}

	static bool cell_rowid ( const uint8_t* page, uint32_t slot, uint32_t count, bool leaf, int64_t& out ) {

		uint32_t offset = offset_of ( page, slot );

		if ( offset < BTREE_HEADER + count * 2 || offset >= PAGE_USABLE )
			return false;

		size_t at = leaf ? 0 : 4;
		uint64_t number = 0;
		const uint8_t* bytes = page + offset + at;

		if ( offset + at + 4 <= PAGE_USABLE ) {

			uint32_t a = bytes [ 0 ];

			if ( a < 0x80 ) {

				out = base::unzigzag ( a );

				return true;

			}

			uint32_t b = bytes [ 1 ];

			if ( b < 0x80 ) {

				out = base::unzigzag ( ( a & 0x7F ) | b << 7 );

				return true;

			}

			uint32_t c = bytes [ 2 ];

			if ( c < 0x80 ) {

				out = base::unzigzag ( ( a & 0x7F ) | ( b & 0x7F ) << 7 | c << 14 );

				return true;

			}

			uint32_t d = bytes [ 3 ];

			if ( d < 0x80 ) {

				out = base::unzigzag ( ( a & 0x7F ) | ( b & 0x7F ) << 7 | ( c & 0x7F ) << 14 | d << 21 );

				return true;

			}

		}

		if ( !base::read_varint ( page + offset, PAGE_USABLE - offset, at, number ) )
			return false;

		out = base::unzigzag ( number );

		return true;

	}

	static bool find_slot ( pager& owner, const uint8_t* page, bool table, int64_t rowid, const key_probe* probe, uint32_t& slot, bool& exact ) {

		uint32_t low = 0;
		uint32_t high = count_of ( page );

		exact = false;

		if ( table ) {

			uint32_t count = high;
			bool leaf = leaf_kind ( page [ 0 ] );
			int64_t first = 0;
			int64_t last = 0;

			if ( !count ) {

				slot = 0;

				return true;

			}

			if ( !cell_rowid ( page, count - 1, count, leaf, last ) )
				return false;

			if ( last <= rowid ) {

				slot = last < rowid ? count : count - 1;
				exact = last == rowid;

				return true;

			}

			if ( !cell_rowid ( page, 0, count, leaf, first ) )
				return false;

			if ( rowid <= first ) {

				slot = 0;
				exact = first == rowid;

				return true;

			}

			low = 1;
			high = count - 1;

			for ( uint32_t round = 0; low < high; round++ ) {

				uint32_t middle = low + ( high - low ) / 2;

				if ( round < 2 ) {

					double reach = static_cast < double > ( static_cast < uint64_t > ( rowid ) - static_cast < uint64_t > ( first ) ) / static_cast < double > ( static_cast < uint64_t > ( last ) - static_cast < uint64_t > ( first ) );
					uint32_t guess = low - 1 + static_cast < uint32_t > ( reach * static_cast < double > ( high - low + 1 ) );

					middle = guess < low ? low : guess >= high ? high - 1 : guess;

				}

				int64_t here = 0;

				if ( !cell_rowid ( page, middle, count, leaf, here ) )
					return false;

				if ( here == rowid ) {

					slot = middle;
					exact = true;

					return true;

				}

				if ( here < rowid ) {

					low = middle + 1;
					first = here;

				} else {

					high = middle;
					last = here;

				}

			}

			slot = low;

			return true;

		}

		uint32_t count = high;

		if ( high ) {

			bool fine = true;
			int32_t order = index_order ( owner, page, high - 1, count, *probe, fine );

			if ( !fine )
				return false;

			if ( order < 0 ) {

				slot = high;

				return true;

			}

			exact = order == 0;
			high--;

		}

		while ( low < high ) {

			uint32_t middle = low + ( high - low ) / 2;
			bool fine = true;
			int32_t order = index_order ( owner, page, middle, count, *probe, fine );

			if ( !fine )
				return false;

			if ( order < 0 )
				low = middle + 1;
			else {

				high = middle;
				exact = exact || order == 0;

			}

		}

		slot = low;

		return true;

	}

	static bool child_for ( pager& owner, const uint8_t* page, bool table, int64_t rowid, const key_probe* probe, uint32_t& position, uint32_t& child ) {

		bool exact = false;

		if ( !find_slot ( owner, page, table, rowid, probe, position, exact ) )
			return false;

		if ( position < count_of ( page ) ) {

			cell_info info;

			if ( !parse_cell ( page, position, info ) )
				return false;

			child = info.child;

		} else
			child = link_a ( page );

		return child != 0;

	}

	bool create_btree ( pager& owner, bool index, uint32_t& root ) {

		page_frame* frame = new_page ( owner, index ? PAGE_INDEX_LEAF : PAGE_TABLE_LEAF );

		if ( !frame )
			return false;

		base::write16 ( frame->data + 4, PAGE_USABLE );
		root = frame->number;
		release_page ( frame );

		return true;

	}

	static bool drop_page ( pager& owner, uint32_t number, uint32_t depth ) {

		if ( depth > BTREE_DEPTH ) {

			corrupt ( owner, "a tree is too deep", number );

			return false;

		}

		page_frame* frame = get_page ( owner, number );

		if ( !frame )
			return false;

		if ( !tree_kind ( frame->data [ 0 ] ) ) {

			release_page ( frame );
			corrupt ( owner, "a tree points at a page that is not part of a tree", number );

			return false;

		}

		uint32_t children [ BTREE_CELLS + 1 ];
		uint32_t chains [ BTREE_CELLS ];
		uint32_t child_count = 0;
		uint32_t chain_count = 0;
		bool leaf = leaf_kind ( frame->data [ 0 ] );

		for ( uint32_t slot = 0; slot < count_of ( frame->data ) && slot < BTREE_CELLS; slot++ ) {

			cell_info info;

			if ( !parse_cell ( frame->data, slot, info ) ) {

				release_page ( frame );
				corrupt ( owner, "a cell runs past its page", number );

				return false;

			}

			if ( !leaf )
				children [ child_count++ ] = info.child;

			if ( info.overflow )
				chains [ chain_count++ ] = info.overflow;

		}

		if ( !leaf )
			children [ child_count++ ] = link_a ( frame->data );

		release_page ( frame );

		for ( uint32_t i = 0; i < chain_count; i++ )
			if ( !free_overflow ( owner, chains [ i ] ) )
				return false;

		for ( uint32_t i = 0; i < child_count; i++ )
			if ( !drop_page ( owner, children [ i ], depth + 1 ) )
				return false;

		return free_page ( owner, number );

	}

	bool drop_btree ( pager& owner, uint32_t root ) {

		return drop_page ( owner, root, 0 );

	}

	static bool split_leaf ( btree& tree, page_frame* frame, cell_span* cells, uint32_t count, split_result& out, bool appending ) {

		pager& owner = *tree.owner;
		uint8_t kind = frame->data [ 0 ];
		uint32_t total = 0;
		uint32_t half = 0;
		uint32_t k = 0;

		for ( uint32_t i = 0; i < count; i++ )
			total += cells [ i ].size + 2;

		while ( k < count - 1 && half + cells [ k ].size + 2 <= total / 2 )
			half += cells [ k++ ].size + 2;

		if ( !k )
			k = 1;

		if ( appending && count >= 2 && !link_a ( frame->data ) )
			k = count - 1;

		page_frame* right = new_page ( owner, static_cast < page_kind > ( kind ) );

		if ( !right )
			return false;

		uint32_t next = link_a ( frame->data );
		uint32_t previous = link_b ( frame->data );

		if ( !rebuild ( right->data, kind, cells + k, count - k, next, frame->number ) || !rebuild ( frame->data, kind, cells, k, right->number, previous ) ) {

			release_page ( right );
			corrupt ( owner, "a split did not fit", frame->number );

			return false;

		}

		dirty_page ( frame );

		if ( next ) {

			page_frame* after = get_page ( owner, next );

			if ( !after || !change_page ( owner, after ) ) {

				release_page ( after );
				release_page ( right );

				return false;

			}

			base::write32 ( after->data + 12, right->number );
			dirty_page ( after );
			release_page ( after );

		}

		cell_info last;

		if ( !parse_span ( cells [ k - 1 ], kind, last ) ) {

			release_page ( right );
			corrupt ( owner, "a cell could not be read during a split", frame->number );

			return false;

		}

		out.happened = true;
		out.right = right->number;
		out.body.clear ( );
		release_page ( right );

		if ( !tree.index ) {

			out.rowid = last.rowid;

			return true;

		}

		base::text whole;

		return read_full ( owner, last, whole ) && payload_body ( owner, reinterpret_cast < const uint8_t* > ( whole.value ( ) ), whole.size, out.body );

	}

	static bool split_interior ( btree& tree, page_frame* frame, cell_span* cells, uint32_t count, uint32_t right_child, split_result& out, bool appending ) {

		pager& owner = *tree.owner;
		uint8_t kind = frame->data [ 0 ];
		uint32_t total = 0;
		uint32_t half = 0;
		uint32_t m = 0;

		for ( uint32_t i = 0; i < count; i++ )
			total += cells [ i ].size + 2;

		while ( m < count - 2 && half + cells [ m ].size + 2 <= total / 2 )
			half += cells [ m++ ].size + 2;

		if ( !m )
			m = 1;

		if ( appending && count >= 3 )
			m = count - 2;

		cell_info middle;

		if ( !parse_span ( cells [ m ], kind, middle ) ) {

			corrupt ( owner, "a cell could not be read during a split", frame->number );

			return false;

		}

		page_frame* right = new_page ( owner, static_cast < page_kind > ( kind ) );

		if ( !right )
			return false;

		if ( !rebuild ( right->data, kind, cells + m + 1, count - m - 1, right_child, 0 ) || !rebuild ( frame->data, kind, cells, m, middle.child, 0 ) ) {

			release_page ( right );
			corrupt ( owner, "a split did not fit", frame->number );

			return false;

		}

		dirty_page ( frame );
		out.happened = true;
		out.right = right->number;
		out.rowid = middle.rowid;
		out.body.clear ( );
		out.body.add ( reinterpret_cast < const char* > ( cells [ m ].data + 4 ), cells [ m ].size - 4 );
		release_page ( right );

		return true;

	}

	static bool insert_into ( btree& tree, uint32_t number, insert_job& job, split_result& out, uint32_t depth ) {

		pager& owner = *tree.owner;

		out.happened = false;

		if ( depth > BTREE_DEPTH ) {

			corrupt ( owner, "a tree is too deep", number );

			return false;

		}

		page_frame* frame = get_page ( owner, number );

		if ( !frame )
			return false;

		uint8_t kind = frame->data [ 0 ];

		if ( !tree_kind ( kind ) || index_kind ( kind ) != tree.index ) {

			release_page ( frame );
			corrupt ( owner, "a tree points at a page of the wrong kind", number );

			return false;

		}

		uint8_t* scratch = nullptr;
		cell_span* cells = nullptr;
		bool fine = true;
		uint32_t used = 0;
		uint32_t count = 0;

		if ( leaf_kind ( kind ) ) {

			uint32_t slot = 0;
			bool exact = false;

			fine = find_slot ( owner, frame->data, job.table, job.rowid, job.probe, slot, exact );

			if ( fine && exact && ( !job.table || !job.replace ) ) {

				job.existed = true;
				release_page ( frame );

				return true;

			}

			cell_info old = { };

			if ( fine && exact ) {

				job.existed = true;
				fine = parse_cell ( frame->data, slot, old ) && ( !old.overflow || free_overflow ( owner, old.overflow ) );

			}

			fine = fine && change_page ( owner, frame );

			bool placed = fine && ( exact ? swap_cell ( frame->data, slot, old.size, job.cell, job.cell_size ) : place_cell ( frame->data, slot, job.cell, job.cell_size ) );

			if ( fine && !placed )
				fine = borrow_space ( owner, scratch, cells ) && gather ( owner, frame, scratch, used, cells, count );

			if ( fine && !placed ) {

				if ( exact )
					cells [ slot ] = { job.cell, job.cell_size };
				else {

					base::move ( cells + slot + 1, cells + slot, sizeof ( cell_span ) * ( count - slot ) );
					cells [ slot ] = { job.cell, job.cell_size };
					count++;

				}

				if ( rebuild ( frame->data, kind, cells, count, link_a ( frame->data ), link_b ( frame->data ) ) )
					dirty_page ( frame );
				else
					fine = split_leaf ( tree, frame, cells, count, out, !exact && slot == count - 1 );

			}

		} else {

			uint32_t position = 0;
			uint32_t child = 0;

			fine = child_for ( owner, frame->data, job.table, job.rowid, job.probe, position, child );

			if ( !fine && owner.error == PAGER_FINE )
				corrupt ( owner, "an interior page has no child", number );

			split_result below;

			fine = fine && insert_into ( tree, child, job, below, depth + 1 );

			if ( fine && below.happened ) {

				base::text made;

				add_u32 ( made, child );

				if ( job.table )
					add_varint ( made, base::zigzag ( below.rowid ) );
				else
					made.add ( below.body.value ( ), below.body.size );

				fine = change_page ( owner, frame );

				uint32_t before = count_of ( frame->data );

				if ( fine && place_cell ( frame->data, position, reinterpret_cast < const uint8_t* > ( made.value ( ) ), static_cast < uint32_t > ( made.size ) ) ) {

					if ( position < before )
						base::write32 ( frame->data + offset_of ( frame->data, position + 1 ), below.right );
					else
						base::write32 ( frame->data + 8, below.right );

					release_page ( frame );

					return true;

				}

				fine = fine && borrow_space ( owner, scratch, cells ) && gather ( owner, frame, scratch, used, cells, count );

				uint32_t right_child = link_a ( frame->data );
				bool appending = position == count;

				if ( fine ) {

					if ( position < count )
						base::write32 ( const_cast < uint8_t* > ( cells [ position ].data ), below.right );
					else
						right_child = below.right;

					base::move ( cells + position + 1, cells + position, sizeof ( cell_span ) * ( count - position ) );
					cells [ position ] = { reinterpret_cast < const uint8_t* > ( made.value ( ) ), static_cast < uint32_t > ( made.size ) };
					count++;

					if ( rebuild ( frame->data, kind, cells, count, right_child, 0 ) )
						dirty_page ( frame );
					else
						fine = split_interior ( tree, frame, cells, count, right_child, out, appending );

				}

			}

		}

		release_page ( frame );
		return_space ( owner, scratch, cells );

		return fine;

	}

	static bool grow_root ( btree& tree, split_result& split ) {

		pager& owner = *tree.owner;
		page_frame* root = get_page ( owner, tree.root );

		if ( !root )
			return false;

		if ( !change_page ( owner, root ) ) {

			release_page ( root );

			return false;

		}

		uint8_t kind = root->data [ 0 ];
		page_frame* left = new_page ( owner, static_cast < page_kind > ( kind ) );

		if ( !left ) {

			release_page ( root );

			return false;

		}

		base::copy ( left->data, root->data, PAGE_USABLE );

		if ( leaf_kind ( kind ) ) {

			page_frame* right = get_page ( owner, split.right );

			if ( !right || !change_page ( owner, right ) ) {

				release_page ( right );

				release_page ( left );
				release_page ( root );

				return false;

			}

			base::write32 ( right->data + 12, left->number );
			dirty_page ( right );
			release_page ( right );

		}

		base::text made;

		add_u32 ( made, left->number );

		if ( tree.index )
			made.add ( split.body.value ( ), split.body.size );
		else
			add_varint ( made, base::zigzag ( split.rowid ) );

		cell_span cell = { reinterpret_cast < const uint8_t* > ( made.value ( ) ), static_cast < uint32_t > ( made.size ) };
		uint8_t interior = tree.index ? PAGE_INDEX_INTERIOR : PAGE_TABLE_INTERIOR;
		bool fine = rebuild ( root->data, interior, &cell, 1, split.right, 0 );

		dirty_page ( root );
		release_page ( left );
		release_page ( root );

		return fine;

	}

	static bool insert ( btree& tree, insert_job& job ) {

		split_result split;

		if ( !insert_into ( tree, tree.root, job, split, 0 ) )
			return false;

		return !split.happened || grow_root ( tree, split );

	}

	bool table_insert ( btree& tree, int64_t rowid, const uint8_t* payload, size_t size, bool replace, bool& existed ) {

		base::text& cell = tree.owner->cell_bytes;

		cell.clear ( );
		add_varint ( cell, base::zigzag ( rowid ) );

		if ( !payload_body ( *tree.owner, payload, size, cell ) )
			return false;

		insert_job job = { true, rowid, nullptr, 0, reinterpret_cast < const uint8_t* > ( cell.value ( ) ), static_cast < uint32_t > ( cell.size ), replace, false };
		bool fine = insert ( tree, job );

		existed = job.existed;

		if ( fine && existed && !replace && size > BTREE_MAX_LOCAL ) {

			cell_info made;
			cell_span span = { job.cell, job.cell_size };

			if ( parse_span ( span, PAGE_TABLE_LEAF, made ) && made.overflow )
				free_overflow ( *tree.owner, made.overflow );

		}

		return fine;

	}

	bool table_replace_at ( btree& tree, uint32_t number, uint32_t slot, int64_t rowid, const uint8_t* payload, size_t size, bool& done ) {

		pager& owner = *tree.owner;

		done = false;

		if ( size > BTREE_MAX_LOCAL || !number )
			return true;

		page_frame* frame = get_page ( owner, number );

		if ( !frame )
			return false;

		cell_info old;
		uint32_t count = count_of ( frame->data );

		if ( frame->data [ 0 ] != PAGE_TABLE_LEAF || slot >= count || !parse_cell ( frame->data, slot, old ) || old.rowid != rowid || old.overflow ) {

			release_page ( frame );

			return true;

		}

		uint8_t cell [ BTREE_MAX_LOCAL + 24 ];
		size_t used = base::write_varint ( cell, base::zigzag ( rowid ) );

		used += base::write_varint ( cell + used, size );

		if ( size )
			base::copy ( cell + used, payload, size );

		used += size;

		if ( !change_page ( owner, frame ) ) {

			release_page ( frame );

			return false;

		}

		if ( swap_cell ( frame->data, slot, old.size, cell, static_cast < uint32_t > ( used ) ) ) {

			dirty_page ( frame );
			done = true;

		}

		release_page ( frame );

		return true;

	}

	bool table_delete_at ( btree& tree, uint32_t number, uint32_t slot, int64_t rowid, bool& done ) {

		pager& owner = *tree.owner;

		done = false;

		if ( !number )
			return true;

		page_frame* frame = get_page ( owner, number );

		if ( !frame )
			return false;

		cell_info old;
		uint32_t count = count_of ( frame->data );
		bool usable = frame->data [ 0 ] == PAGE_TABLE_LEAF && slot < count && parse_cell ( frame->data, slot, old ) && old.rowid == rowid && !old.overflow;

		if ( usable && number != tree.root )
			usable = quick_used ( frame->data ) >= BTREE_UNDERFULL + old.size + 2;

		if ( !usable ) {

			release_page ( frame );

			return true;

		}

		if ( !change_page ( owner, frame ) ) {

			release_page ( frame );

			return false;

		}

		drop_cell ( frame->data, slot, old.size );
		dirty_page ( frame );
		release_page ( frame );
		done = true;

		return true;

	}

	bool index_insert ( btree& tree, const uint8_t* key, size_t size, bool& existed ) {

		base::text& cell = tree.owner->cell_bytes;

		cell.clear ( );

		if ( !payload_body ( *tree.owner, key, size, cell ) )
			return false;

		key_probe probe;

		make_probe ( key, size, 0, probe );

		insert_job job = { false, 0, key, size, reinterpret_cast < const uint8_t* > ( cell.value ( ) ), static_cast < uint32_t > ( cell.size ), false, false, &probe };
		bool fine = insert ( tree, job );

		existed = job.existed;

		if ( fine && existed && size > BTREE_MAX_LOCAL ) {

			cell_info made;
			cell_span span = { job.cell, job.cell_size };

			if ( parse_span ( span, PAGE_INDEX_LEAF, made ) && made.overflow )
				free_overflow ( *tree.owner, made.overflow );

		}

		return fine;

	}

	static bool merge_children ( btree& tree, page_frame* parent, uint32_t a_slot, cell_span* cells, uint32_t& count, uint32_t& right_child, uint8_t* scratch, uint32_t& used, bool& merged ) {

		pager& owner = *tree.owner;
		cell_info separator;

		merged = false;

		if ( !parse_span ( cells [ a_slot ], parent->data [ 0 ], separator ) )
			return false;

		uint32_t a_number = separator.child;
		uint32_t b_number = a_slot + 1 < count ? 0 : right_child;

		if ( !b_number ) {

			cell_info next;

			if ( !parse_span ( cells [ a_slot + 1 ], parent->data [ 0 ], next ) )
				return false;

			b_number = next.child;

		}

		page_frame* a = get_page ( owner, a_number );
		page_frame* b = a ? get_page ( owner, b_number ) : nullptr;

		if ( !a || !b ) {

			release_page ( a );

			return false;

		}

		uint8_t kind = a->data [ 0 ];

		if ( kind != b->data [ 0 ] ) {

			release_page ( a );
			release_page ( b );
			corrupt ( owner, "siblings of different kinds", parent->number );

			return false;

		}

		uint8_t* local = base::make < uint8_t > ( PAGE_SIZE * 2 + 64 );
		cell_span* joined = base::make < cell_span > ( BTREE_CELLS * 2 + 1 );
		uint32_t local_used = 0;
		uint32_t joined_count = 0;
		bool fine = local && joined && gather ( owner, a, local, local_used, joined, joined_count );
		base::text middle;

		if ( fine && !leaf_kind ( kind ) ) {

			add_u32 ( middle, link_a ( a->data ) );
			middle.add ( reinterpret_cast < const char* > ( cells [ a_slot ].data + 4 ), cells [ a_slot ].size - 4 );
			joined [ joined_count++ ] = { reinterpret_cast < const uint8_t* > ( middle.value ( ) ), static_cast < uint32_t > ( middle.size ) };

		}

		bool room = joined_count + count_of ( b->data ) <= BTREE_CELLS;

		fine = fine && ( !room || gather ( owner, b, local, local_used, joined, joined_count ) );

		uint8_t* built = fine && room ? base::make < uint8_t > ( PAGE_SIZE ) : nullptr;
		bool leaf = leaf_kind ( kind );
		uint32_t after = leaf ? link_a ( b->data ) : 0;

		if ( fine && built && rebuild ( built, kind, joined, joined_count, leaf ? after : link_a ( b->data ), leaf ? link_b ( a->data ) : 0 ) && ( fine = change_page ( owner, a ) ) ) {

			base::copy ( a->data, built, PAGE_USABLE );
			dirty_page ( a );
			merged = true;

			if ( leaf && after ) {

				page_frame* next = get_page ( owner, after );

				if ( next && !change_page ( owner, next ) ) {

					release_page ( next );
					next = nullptr;

				}

				if ( next ) {

					base::write32 ( next->data + 12, a_number );
					dirty_page ( next );
					release_page ( next );

				} else
					fine = false;

			}

		}

		base::release ( built );
		release_page ( a );
		release_page ( b );
		base::release ( local );
		base::release ( joined );

		if ( !fine || !merged )
			return fine;

		if ( leaf && tree.index && separator.overflow && !free_overflow ( owner, separator.overflow ) )
			return false;

		if ( !free_page ( owner, b_number ) )
			return false;

		if ( a_slot + 1 < count ) {

			base::copy ( scratch + used, cells [ a_slot + 1 ].data, cells [ a_slot + 1 ].size );
			base::write32 ( scratch + used, a_number );
			cells [ a_slot + 1 ] = { scratch + used, cells [ a_slot + 1 ].size };
			used += cells [ a_slot + 1 ].size;

		} else
			right_child = a_number;

		base::move ( cells + a_slot, cells + a_slot + 1, sizeof ( cell_span ) * ( count - a_slot - 1 ) );
		count--;

		return true;

	}

	static bool remove_from ( btree& tree, uint32_t number, const remove_job& job, remove_result& out, uint32_t depth ) {

		pager& owner = *tree.owner;

		out = { };

		if ( depth > BTREE_DEPTH ) {

			corrupt ( owner, "a tree is too deep", number );

			return false;

		}

		page_frame* frame = get_page ( owner, number );

		if ( !frame )
			return false;

		uint8_t kind = frame->data [ 0 ];

		if ( !tree_kind ( kind ) || index_kind ( kind ) != tree.index ) {

			release_page ( frame );
			corrupt ( owner, "a tree points at a page of the wrong kind", number );

			return false;

		}

		uint8_t* scratch = nullptr;
		cell_span* cells = nullptr;
		bool fine = true;
		uint32_t used = 0;
		uint32_t count = 0;

		if ( leaf_kind ( kind ) ) {

			uint32_t slot = 0;
			bool exact = false;

			fine = find_slot ( owner, frame->data, job.table, job.rowid, job.probe, slot, exact );

			if ( fine && exact ) {

				cell_info old;

				fine = parse_cell ( frame->data, slot, old ) && ( !old.overflow || free_overflow ( owner, old.overflow ) ) && change_page ( owner, frame );

				if ( fine ) {

					drop_cell ( frame->data, slot, old.size );
					dirty_page ( frame );
					out.found = true;

				}

			}

		} else {

			uint32_t position = 0;
			uint32_t child = 0;

			fine = child_for ( owner, frame->data, job.table, job.rowid, job.probe, position, child );

			remove_result below;

			fine = fine && remove_from ( tree, child, job, below, depth + 1 );
			out.found = below.found;

			if ( fine && below.underfull && count_of ( frame->data ) ) {

				uint32_t right_child = link_a ( frame->data );
				bool merged = false;

				fine = borrow_space ( owner, scratch, cells ) && gather ( owner, frame, scratch, used, cells, count );

				uint32_t a_slot = position < count ? position : count - 1;

				fine = fine && merge_children ( tree, frame, a_slot, cells, count, right_child, scratch, used, merged );

				if ( fine && merged && ( fine = change_page ( owner, frame ) ) ) {

					fine = rebuild ( frame->data, kind, cells, count, right_child, 0 );
					dirty_page ( frame );

				}

			}

		}

		if ( fine )
			out.underfull = quick_used ( frame->data ) < BTREE_UNDERFULL;

		release_page ( frame );
		return_space ( owner, scratch, cells );

		return fine;

	}

	static bool shrink_root ( btree& tree ) {

		pager& owner = *tree.owner;

		for ( uint32_t guard = 0; guard < BTREE_DEPTH; guard++ ) {

			page_frame* root = get_page ( owner, tree.root );

			if ( !root )
				return false;

			if ( leaf_kind ( root->data [ 0 ] ) || count_of ( root->data ) ) {

				release_page ( root );

				return true;

			}

			uint32_t only = link_a ( root->data );
			page_frame* child = change_page ( owner, root ) ? get_page ( owner, only ) : nullptr;

			if ( !child ) {

				release_page ( root );

				return false;

			}

			base::copy ( root->data, child->data, PAGE_USABLE );

			if ( leaf_kind ( root->data [ 0 ] ) ) {

				base::write32 ( root->data + 8, 0 );
				base::write32 ( root->data + 12, 0 );

			}

			dirty_page ( root );
			release_page ( child );
			release_page ( root );

			if ( !free_page ( owner, only ) )
				return false;

		}

		return true;

	}

	static bool remove ( btree& tree, const remove_job& job, bool& found ) {

		remove_result result;

		if ( !remove_from ( tree, tree.root, job, result, 0 ) )
			return false;

		found = result.found;

		return shrink_root ( tree );

	}

	bool table_delete ( btree& tree, int64_t rowid, bool& found ) {

		remove_job job = { true, rowid, nullptr, 0 };

		return remove ( tree, job, found );

	}

	bool index_delete ( btree& tree, const uint8_t* key, size_t size, bool& found ) {

		key_probe probe;

		make_probe ( key, size, 0, probe );

		remove_job job = { false, 0, key, size, &probe };

		return remove ( tree, job, found );

	}

	static bool settle ( cursor& at ) {

		pager& owner = *at.tree.owner;

		for ( uint32_t guard = 0; at.page && guard <= owner.header.page_count; guard++ ) {

			page_frame* frame = get_page ( owner, at.page );

			if ( !frame )
				return false;

			if ( !leaf_kind ( frame->data [ 0 ] ) ) {

				release_page ( frame );
				corrupt ( owner, "a leaf link points at a page that is not a leaf", at.page );

				return false;

			}

			uint32_t count = count_of ( frame->data );
			uint32_t next = link_a ( frame->data );

			release_page ( frame );

			if ( at.slot < count ) {

				at.valid = true;

				return true;

			}

			at.page = next;
			at.slot = 0;

		}

		at.valid = false;

		return at.page == 0;

	}

	static leaf_hint* hint_for ( pager& owner, uint32_t root ) {

		for ( uint32_t i = 0; i < PAGER_LEAVES; i++ )
			if ( owner.leaves [ i ].root == root )
				return &owner.leaves [ i ];

		return nullptr;

	}

	static void remember_leaf ( pager& owner, leaf_hint* hint, uint32_t root, uint32_t leaf, uint32_t slot ) {

		if ( !hint ) {

			hint = &owner.leaves [ owner.next_leaf++ % PAGER_LEAVES ];
			hint->root = root;
			hint->score = 1;
			hint->tries = 0;

		}

		hint->leaf = leaf;
		hint->slot = slot;
		hint->layout = owner.layout;

	}

	static bool gallop_seek ( pager& owner, const uint8_t* page, uint32_t count, uint32_t start, const key_probe& probe, uint32_t& slot, bool& exact, bool& hit ) {

		bool fine = true;
		uint32_t s = start < 1 ? 1 : start > count - 1 ? count - 1 : start;

		hit = false;

		if ( index_order ( owner, page, s - 1, count, probe, fine ) >= 0 || !fine )
			return fine;

		uint32_t low = s - 1;
		uint32_t high = 0;
		uint32_t step = 1;
		bool found = false;

		exact = false;

		while ( !found ) {

			uint32_t next = low + step;

			if ( next >= count - 1 ) {

				int32_t order = index_order ( owner, page, count - 1, count, probe, fine );

				if ( !fine || order < 0 )
					return fine;

				high = count - 1;
				exact = order == 0;
				found = true;

				break;

			}

			int32_t order = index_order ( owner, page, next, count, probe, fine );

			if ( !fine )
				return false;

			if ( order >= 0 ) {

				high = next;
				exact = order == 0;
				found = true;

			} else {

				low = next;
				step *= 2;

			}

		}

		low++;

		while ( fine && low < high ) {

			uint32_t middle = low + ( high - low ) / 2;
			int32_t order = index_order ( owner, page, middle, count, probe, fine );

			if ( order < 0 )
				low = middle + 1;
			else {

				high = middle;
				exact = exact || order == 0;

			}

		}

		slot = low;
		hit = fine;

		return fine;

	}

	static bool hinted_seek ( pager& owner, leaf_hint& hint, const key_probe& probe, cursor& at, bool& hit ) {

		page_frame* frame = get_page ( owner, hint.leaf );

		hit = false;

		if ( !frame )
			return false;

		const uint8_t* page = frame->data;
		uint32_t count = count_of ( page );
		bool fine = true;

		if ( page [ 0 ] == PAGE_INDEX_LEAF && count >= 2 ) {

			uint32_t slot = 0;
			bool exact = false;

			fine = gallop_seek ( owner, page, count, hint.slot, probe, slot, exact, hit );

			if ( fine && hit ) {

				at.page = hint.leaf;
				at.slot = slot;
				at.valid = true;
				at.match = exact ? SEEK_EQUAL : SEEK_NONE;
				hint.slot = slot;
				release_page ( frame );

				return true;

			}

		}

		if ( fine && page [ 0 ] == PAGE_INDEX_LEAF && count >= 2 ) {

			int32_t low_order = index_order ( owner, page, 0, count, probe, fine );
			int32_t high_order = fine && low_order < 0 ? index_order ( owner, page, count - 1, count, probe, fine ) : -1;

			if ( fine && low_order < 0 && high_order >= 0 ) {

				uint32_t low = 1;
				uint32_t high = count - 1;
				bool exact = high_order == 0;

				while ( fine && low < high ) {

					uint32_t middle = low + ( high - low ) / 2;
					int32_t order = index_order ( owner, page, middle, count, probe, fine );

					if ( order < 0 )
						low = middle + 1;
					else {

						high = middle;
						exact = exact || order == 0;

					}

				}

				if ( fine ) {

					at.page = hint.leaf;
					at.slot = low;
					at.valid = true;
					at.match = exact ? SEEK_EQUAL : SEEK_NONE;
					hint.slot = low;
					hit = true;

				}

			}

		}

		release_page ( frame );

		return fine;

	}

	static bool descend ( cursor& at, btree& tree, bool first, int64_t rowid, const uint8_t* key, size_t size, uint32_t columns ) {

		pager& owner = *tree.owner;
		uint32_t number = tree.root;
		key_probe probe;
		leaf_hint* hint = nullptr;
		bool hinted = tree.index && !first;

		at.tree = tree;
		at.valid = false;
		at.match = SEEK_UNKNOWN;
		at.frame = nullptr;

		if ( hinted ) {

			make_probe ( key, size, columns, probe );
			hint = hint_for ( owner, tree.root );

			if ( hint && hint->layout == owner.layout && ( hint->score > 0 || ( ++hint->tries & 15 ) == 0 ) ) {

				bool hit = false;

				if ( !hinted_seek ( owner, *hint, probe, at, hit ) )
					return false;

				if ( hit ) {

					hint->score = hint->score < 8 ? hint->score + 1 : 8;

					return true;

				}

				hint->score = hint->score > -8 ? hint->score - 1 : -8;

			}

		}

		for ( uint32_t depth = 0; depth <= BTREE_DEPTH; depth++ ) {

			page_frame* frame = depth < PAGER_PATH ? reuse_page ( owner, owner.descent [ depth ], number ) : get_page ( owner, number );

			if ( !frame )
				return false;

			if ( depth < PAGER_PATH )
				owner.descent [ depth ] = frame;

			uint8_t kind = frame->data [ 0 ];

			if ( !tree_kind ( kind ) || index_kind ( kind ) != tree.index ) {

				release_page ( frame );
				corrupt ( owner, "a tree points at a page of the wrong kind", number );

				return false;

			}

			if ( leaf_kind ( kind ) ) {

				uint32_t slot = 0;
				bool exact = false;
				bool fine = first || find_slot ( owner, frame->data, !tree.index, rowid, &probe, slot, exact );
				uint32_t count = count_of ( frame->data );

				release_page ( frame );

				if ( !fine )
					return false;

				if ( hinted ) {

					remember_leaf ( owner, hint, tree.root, number, slot );

					if ( slot < count )
						at.match = exact ? SEEK_EQUAL : SEEK_NONE;

				}

				at.page = number;
				at.slot = slot;

				if ( slot < count ) {

					at.valid = true;

					return true;

				}

				return settle ( at );

			}

			uint32_t position = 0;
			uint32_t child = 0;
			bool fine = true;

			if ( first ) {

				cell_info info;

				if ( count_of ( frame->data ) && parse_cell ( frame->data, 0, info ) )
					child = info.child;
				else
					child = link_a ( frame->data );

				fine = child != 0;

			} else
				fine = child_for ( owner, frame->data, !tree.index, rowid, &probe, position, child );

			release_page ( frame );

			if ( !fine ) {

				if ( owner.error == PAGER_FINE )
					corrupt ( owner, "an interior page has no child", number );

				return false;

			}

			number = child;

		}

		corrupt ( owner, "a tree is too deep", tree.root );

		return false;

	}

	bool cursor_first ( cursor& at, btree& tree ) {

		return descend ( at, tree, true, 0, nullptr, 0, 0 );

	}

	bool cursor_seek_rowid ( cursor& at, btree& tree, int64_t rowid ) {

		return descend ( at, tree, false, rowid, nullptr, 0, 0 );

	}

	bool cursor_seek_key ( cursor& at, btree& tree, const uint8_t* key, size_t size, uint32_t columns ) {

		return descend ( at, tree, false, 0, key, size, columns );

	}

	bool cursor_next ( cursor& at ) {

		if ( !at.valid )
			return true;

		at.slot++;

		return settle ( at );

	}

	static bool current_cell ( cursor& at, cell_info& info ) {

		if ( !at.valid )
			return false;

		page_frame* frame = get_page ( *at.tree.owner, at.page );

		if ( !frame )
			return false;

		bool fine = at.slot < count_of ( frame->data ) && parse_cell ( frame->data, at.slot, info );

		release_page ( frame );

		return fine;

	}

	bool cursor_rowid ( cursor& at, int64_t& rowid ) {

		cell_info info;

		if ( !current_cell ( at, info ) )
			return false;

		rowid = info.rowid;

		return true;

	}

	bool cursor_payload ( cursor& at, base::text& out ) {

		if ( !at.valid )
			return false;

		page_frame* frame = get_page ( *at.tree.owner, at.page );

		if ( !frame )
			return false;

		cell_info info;
		bool fine = at.slot < count_of ( frame->data ) && parse_cell ( frame->data, at.slot, info );

		fine = fine && read_full ( *at.tree.owner, info, out );
		release_page ( frame );

		return fine;

	}

	bool cursor_view ( cursor& at, base::arena& memory, int64_t& rowid, const uint8_t*& data, size_t& size, page_frame*& held ) {

		held = nullptr;

		if ( !at.valid )
			return false;

		pager& owner = *at.tree.owner;
		page_frame* frame = reuse_page ( owner, at.frame, at.page );

		if ( !frame )
			return false;

		at.frame = frame;

		cell_info info;
		uint32_t count = count_of ( frame->data );
		bool fine = at.slot < count && parse_cell ( frame->data, at.slot, info );
		bool kept_frame = false;

		if ( fine && info.payload == info.local ) {

			data = info.local_data;
			size = info.local;
			kept_frame = true;

		} else if ( fine ) {

			base::text whole;

			fine = read_full ( owner, info, whole );

			char* kept = fine ? memory.keep ( whole.value ( ), whole.size ) : nullptr;

			fine = kept != nullptr;
			data = reinterpret_cast < const uint8_t* > ( kept );
			size = whole.size;

		}

		uint32_t next = link_a ( frame->data );

		rowid = info.rowid;

		if ( fine && kept_frame )
			held = frame;
		else
			release_page ( frame );

		if ( !fine )
			return false;

		at.slot++;

		if ( at.slot < count )
			return true;

		at.page = next;
		at.slot = 0;

		if ( settle ( at ) )
			return true;

		if ( held )
			release_page ( held );

		held = nullptr;

		return false;

	}

	static bool step_page ( cursor& at, uint32_t next ) {

		at.page = next;
		at.slot = 0;

		return settle ( at );

	}

	bool cursor_scan ( cursor& at, base::arena& memory, row_filter filter, void* context, scan_result& out ) {

		out.outcome = SCAN_END;
		out.examined = 0;

		while ( at.valid ) {

			pager& owner = *at.tree.owner;
			page_frame* frame = reuse_page ( owner, at.frame, at.page );

			if ( !frame )
				return false;

			at.frame = frame;

			const uint8_t* page = frame->data;
			uint32_t count = count_of ( page );
			uint32_t next = link_a ( page );

			while ( at.slot < count ) {

				cell_info info;

				if ( !parse_cell ( page, at.slot, info ) ) {

					release_page ( frame );
					corrupt ( owner, "a cell runs past its page", at.page );

					return false;

				}

				const uint8_t* data = info.local_data;
				size_t size = info.local;
				bool copied = false;
				base::arena_mark before = { };

				if ( info.payload != info.local ) {

					base::text whole;

					before = memory.mark ( );

					if ( !read_full ( owner, info, whole ) ) {

						release_page ( frame );

						return false;

					}

					data = reinterpret_cast < const uint8_t* > ( memory.keep ( whole.value ( ), whole.size ) );
					size = whole.size;
					copied = true;

					if ( !data ) {

						release_page ( frame );

						return false;

					}

				}

				out.examined++;

				scan_outcome verdict = filter ( context, info.rowid, data, size );

				if ( verdict == SCAN_REJECT ) {

					if ( copied )
						memory.rewind ( before );

					at.slot++;

					continue;

				}

				if ( verdict != SCAN_TAKE ) {

					release_page ( frame );
					out.outcome = verdict;

					return true;

				}

				if ( !copied ) {

					uint8_t* kept = static_cast < uint8_t* > ( memory.take_raw ( size + 1 ) );

					if ( !kept ) {

						release_page ( frame );

						return false;

					}

					base::copy ( kept, data, size );
					data = kept;

				}

				out.outcome = SCAN_TAKE;
				out.rowid = info.rowid;
				out.data = data;
				out.size = size;
				out.page = at.page;
				out.slot = at.slot;
				release_page ( frame );
				at.slot++;

				return at.slot < count || step_page ( at, next );

			}

			release_page ( frame );

			if ( !step_page ( at, next ) )
				return false;

		}

		return true;

	}

	bool cursor_take ( cursor& at, base::arena& memory, int64_t& rowid, const uint8_t*& data, size_t& size ) {

		if ( !at.valid )
			return false;

		pager& owner = *at.tree.owner;
		page_frame* frame = reuse_page ( owner, at.frame, at.page );

		if ( !frame )
			return false;

		at.frame = frame;

		cell_info info;
		uint32_t count = count_of ( frame->data );
		bool fine = at.slot < count && parse_cell ( frame->data, at.slot, info );

		if ( fine && info.payload == info.local ) {

			uint8_t* kept = static_cast < uint8_t* > ( memory.take_raw ( info.local + 1 ) );

			fine = kept != nullptr;

			if ( fine ) {

				base::copy ( kept, info.local_data, info.local );
				data = kept;
				size = info.local;

			}

		} else if ( fine ) {

			base::text whole;

			fine = read_full ( owner, info, whole );

			char* kept = fine ? memory.keep ( whole.value ( ), whole.size ) : nullptr;

			fine = kept != nullptr;
			data = reinterpret_cast < const uint8_t* > ( kept );
			size = whole.size;

		}

		uint32_t next = link_a ( frame->data );

		rowid = info.rowid;
		release_page ( frame );

		if ( !fine )
			return false;

		at.slot++;

		if ( at.slot < count )
			return true;

		at.page = next;
		at.slot = 0;

		return settle ( at );

	}

	bool table_lookup ( btree& tree, int64_t rowid, base::arena& memory, const uint8_t*& data, size_t& size, bool& found, uint32_t* page, uint32_t* where ) {

		pager& owner = *tree.owner;
		uint32_t number = tree.root;

		found = false;

		for ( uint32_t depth = 0; depth <= BTREE_DEPTH; depth++ ) {

			page_frame* frame = depth < PAGER_PATH ? reuse_page ( owner, owner.descent [ depth ], number ) : get_page ( owner, number );

			if ( !frame )
				return false;

			if ( depth < PAGER_PATH )
				owner.descent [ depth ] = frame;

			uint8_t kind = frame->data [ 0 ];

			if ( kind == PAGE_TABLE_LEAF ) {

				uint32_t slot = 0;
				bool exact = false;
				bool fine = find_slot ( owner, frame->data, true, rowid, nullptr, slot, exact );
				cell_info info;

				if ( fine && exact && ( fine = parse_cell ( frame->data, slot, info ) ) ) {

					found = true;

					if ( page )
						*page = number;

					if ( where )
						*where = slot;

					if ( info.payload == info.local ) {

						uint8_t* kept = static_cast < uint8_t* > ( memory.take_raw ( info.local + 1 ) );

						fine = kept != nullptr;

						if ( fine ) {

							base::copy ( kept, info.local_data, info.local );
							data = kept;
							size = info.local;

						}

					} else {

						base::text whole;

						fine = read_full ( owner, info, whole );

						char* kept = fine ? memory.keep ( whole.value ( ), whole.size ) : nullptr;

						fine = kept != nullptr;
						data = reinterpret_cast < const uint8_t* > ( kept );
						size = whole.size;

					}

				}

				release_page ( frame );

				return fine;

			}

			if ( kind != PAGE_TABLE_INTERIOR ) {

				release_page ( frame );
				corrupt ( owner, "a tree points at a page of the wrong kind", number );

				return false;

			}

			uint32_t position = 0;
			uint32_t child = 0;
			bool fine = child_for ( owner, frame->data, true, rowid, nullptr, position, child );

			release_page ( frame );

			if ( !fine ) {

				if ( owner.error == PAGER_FINE )
					corrupt ( owner, "an interior page has no child", number );

				return false;

			}

			number = child;

		}

		corrupt ( owner, "a tree is too deep", tree.root );

		return false;

	}

	bool table_find ( btree& tree, int64_t rowid, base::text& payload, bool& found ) {

		cursor at;

		found = false;

		if ( !cursor_seek_rowid ( at, tree, rowid ) )
			return false;

		int64_t here = 0;

		if ( !at.valid || !cursor_rowid ( at, here ) || here != rowid )
			return true;

		found = true;

		return cursor_payload ( at, payload );

	}

	static bool hinted_last ( pager& owner, uint32_t root, int64_t& rowid ) {

		for ( uint32_t i = 0; i < PAGER_TAILS; i++ ) {

			const tail_hint& hint = owner.tails [ i ];

			if ( hint.root != root || hint.shape != owner.shape || !hint.leaf )
				continue;

			page_frame* frame = get_page ( owner, hint.leaf );

			if ( !frame )
				return false;

			uint32_t count = count_of ( frame->data );
			bool fine = frame->data [ 0 ] == PAGE_TABLE_LEAF && !link_a ( frame->data ) && count && cell_rowid ( frame->data, count - 1, count, true, rowid );

			release_page ( frame );

			return fine;

		}

		return false;

	}

	static void remember_tail ( pager& owner, uint32_t root, uint32_t leaf ) {

		for ( uint32_t i = 0; i < PAGER_TAILS; i++ ) {

			if ( owner.tails [ i ].root == root ) {

				owner.tails [ i ] = { root, leaf, owner.shape };

				return;

			}

		}

		owner.tails [ owner.next_tail++ % PAGER_TAILS ] = { root, leaf, owner.shape };

	}

	bool table_last ( btree& tree, int64_t& rowid, bool& empty ) {

		pager& owner = *tree.owner;
		uint32_t number = tree.root;

		empty = true;

		if ( hinted_last ( owner, tree.root, rowid ) ) {

			empty = false;

			return true;

		}

		owner.error = PAGER_FINE;

		for ( uint32_t depth = 0; depth <= BTREE_DEPTH; depth++ ) {

			page_frame* frame = get_page ( owner, number );

			if ( !frame )
				return false;

			if ( leaf_kind ( frame->data [ 0 ] ) ) {

				for ( uint32_t guard = 0; guard <= owner.header.page_count; guard++ ) {

					uint32_t count = count_of ( frame->data );

					if ( count ) {

						cell_info info;
						bool fine = parse_cell ( frame->data, count - 1, info );

						if ( fine && !link_a ( frame->data ) )
							remember_tail ( owner, tree.root, frame->number );

						release_page ( frame );
						rowid = info.rowid;
						empty = false;

						return fine;

					}

					uint32_t previous = link_b ( frame->data );

					release_page ( frame );

					if ( !previous )
						return true;

					frame = get_page ( owner, previous );

					if ( !frame )
						return false;

				}

				release_page ( frame );

				return false;

			}

			number = link_a ( frame->data );
			release_page ( frame );

		}

		return false;

	}

	static void report_problem ( tree_report& report, const char* what, uint32_t number ) {

		if ( !report.fine )
			return;

		report.fine = false;
		report.problem.add ( what ).add ( " on page " ).number ( number );

	}

	static bool check_page ( pager& owner, uint32_t number, bool index, uint32_t depth, bool has_low, int64_t low, const base::text* low_key, bool has_high, int64_t high, const base::text* high_key, tree_report& report, leaf_trail& trail ) {

		if ( depth > BTREE_DEPTH ) {

			report_problem ( report, "the tree is too deep", number );

			return false;

		}

		page_frame* frame = get_page ( owner, number );

		if ( !frame ) {

			report_problem ( report, owner.problem, number );

			return false;

		}

		uint8_t kind = frame->data [ 0 ];
		uint32_t count = count_of ( frame->data );

		report.pages++;

		if ( !tree_kind ( kind ) || index_kind ( kind ) != index ) {

			release_page ( frame );
			report_problem ( report, "a page of the wrong kind", number );

			return false;

		}

		if ( BTREE_HEADER + count * 2 > base::read16 ( frame->data + 4 ) || base::read16 ( frame->data + 4 ) > PAGE_USABLE ) {

			release_page ( frame );
			report_problem ( report, "cell pointers overlap cell content", number );

			return false;

		}

		uint8_t* copy = base::make < uint8_t > ( PAGE_SIZE );

		if ( !copy ) {

			release_page ( frame );

			return false;

		}

		base::copy ( copy, frame->data, PAGE_SIZE );
		release_page ( frame );
		report.used_bytes += used_bytes ( copy );

		bool leaf = leaf_kind ( kind );
		bool fine = true;
		bool have_previous = false;
		int64_t previous = 0;
		base::text previous_key;

		if ( leaf ) {

			if ( !report.depth )
				report.depth = depth + 1;
			else if ( report.depth != depth + 1 )
				report_problem ( report, "leaves at different depths", number ), fine = false;

			report.leaves++;

			if ( trail.count < trail.room )
				trail.pages [ trail.count++ ] = number;

		}

		for ( uint32_t slot = 0; slot < count && fine; slot++ ) {

			cell_info info;
			base::text key;
			int64_t rowid = 0;

			if ( !parse_cell ( copy, slot, info ) ) {

				report_problem ( report, "a cell runs past its page", number );
				fine = false;

				break;

			}

			if ( index ) {

				if ( !read_full ( owner, info, key ) ) {

					report_problem ( report, "a key could not be read", number );
					fine = false;

					break;

				}

				const uint8_t* bytes = reinterpret_cast < const uint8_t* > ( key.value ( ) );

				if ( have_previous && compare_records ( reinterpret_cast < const uint8_t* > ( previous_key.value ( ) ), previous_key.size, bytes, key.size, 0 ) >= 0 )
					report_problem ( report, "keys out of order", number ), fine = false;

				if ( has_low && compare_records ( bytes, key.size, reinterpret_cast < const uint8_t* > ( low_key->value ( ) ), low_key->size, 0 ) <= 0 )
					report_problem ( report, "a key below its parent's range", number ), fine = false;

				if ( has_high && compare_records ( bytes, key.size, reinterpret_cast < const uint8_t* > ( high_key->value ( ) ), high_key->size, 0 ) > 0 )
					report_problem ( report, "a key above its parent's range", number ), fine = false;

			} else {

				rowid = info.rowid;

				if ( have_previous && rowid <= previous )
					report_problem ( report, "row ids out of order", number ), fine = false;

				if ( has_low && rowid <= low )
					report_problem ( report, "a row id below its parent's range", number ), fine = false;

				if ( has_high && rowid > high )
					report_problem ( report, "a row id above its parent's range", number ), fine = false;

			}

			if ( leaf ) {

				report.entries++;

				if ( info.overflow ) {

					uint32_t pages = static_cast < uint32_t > ( ( info.payload - info.local + OVERFLOW_ROOM - 1 ) / OVERFLOW_ROOM );

					report.overflow += pages;

					base::text whole;

					if ( !index && !read_full ( owner, info, whole ) )
						report_problem ( report, "an overflow chain is broken", number ), fine = false;

				}

			} else if ( fine ) {

				fine = check_page ( owner, info.child, index, depth + 1, have_previous || has_low, have_previous ? previous : low, have_previous ? &previous_key : low_key, true, rowid, &key, report, trail );

			}

			have_previous = true;
			previous = rowid;
			previous_key.clear ( );
			previous_key.add ( key.value ( ), key.size );

		}

		if ( fine && !leaf )
			fine = check_page ( owner, link_a ( copy ), index, depth + 1, have_previous || has_low, have_previous ? previous : low, have_previous ? &previous_key : low_key, has_high, high, high_key, report, trail );

		base::release ( copy );

		return fine;

	}

	bool check_btree ( pager& owner, uint32_t root, bool index, tree_report& report ) {

		report.pages = 0;
		report.leaves = 0;
		report.overflow = 0;
		report.depth = 0;
		report.entries = 0;
		report.used_bytes = 0;
		report.fine = true;
		report.problem.clear ( );

		leaf_trail trail = { base::make < uint32_t > ( owner.header.page_count + 1 ), 0, owner.header.page_count + 1 };

		if ( !trail.pages )
			return false;

		check_page ( owner, root, index, 0, false, 0, nullptr, false, 0, nullptr, report, trail );

		for ( uint32_t i = 0; i < trail.count && report.fine; i++ ) {

			page_frame* frame = get_page ( owner, trail.pages [ i ] );

			if ( !frame )
				break;

			uint32_t next = link_a ( frame->data );
			uint32_t previous = link_b ( frame->data );

			release_page ( frame );

			if ( next != ( i + 1 < trail.count ? trail.pages [ i + 1 ] : 0 ) || previous != ( i ? trail.pages [ i - 1 ] : 0 ) )
				report_problem ( report, "leaf links do not follow the tree", trail.pages [ i ] );

		}

		base::release ( trail.pages );

		return report.fine;

	}

	bool inspect_page ( pager& owner, uint32_t number, page_summary& out ) {

		page_frame* frame = get_page ( owner, number );

		if ( !frame )
			return false;

		const uint8_t* page = frame->data;

		out.number = number;
		out.kind = page [ 0 ];
		out.cells = 0;
		out.used = 0;
		out.child_count = 0;
		out.overflow_cells = 0;
		out.has_rowids = false;
		out.low_rowid = 0;
		out.high_rowid = 0;
		out.next = 0;
		out.previous = 0;

		if ( tree_kind ( out.kind ) ) {

			bool leaf = leaf_kind ( out.kind );

			out.cells = base::min < uint32_t > ( count_of ( page ), BTREE_CELLS );
			out.used = BTREE_HEADER + used_bytes ( page );

			for ( uint32_t slot = 0; slot < out.cells; slot++ ) {

				cell_info info;

				if ( !parse_cell ( page, slot, info ) )
					continue;

				if ( !leaf )
					out.children [ out.child_count++ ] = info.child;

				if ( info.overflow )
					out.overflow_cells++;

				if ( !index_kind ( out.kind ) ) {

					if ( !out.has_rowids || info.rowid < out.low_rowid )
						out.low_rowid = info.rowid;

					if ( !out.has_rowids || info.rowid > out.high_rowid )
						out.high_rowid = info.rowid;

					out.has_rowids = true;

				}

			}

			if ( leaf ) {

				out.next = link_a ( page );
				out.previous = link_b ( page );

			} else
				out.children [ out.child_count++ ] = link_a ( page );

		} else if ( out.kind == PAGE_OVERFLOW ) {

			out.next = base::read32 ( page + 4 );
			out.used = OVERFLOW_HEADER + base::read32 ( page + 8 );

		} else if ( out.kind == PAGE_FREE ) {

			out.next = base::read32 ( page + 4 );

		} else
			out.used = PAGE_SIZE;

		out.free = out.used < PAGE_USABLE ? PAGE_USABLE - out.used : 0;
		release_page ( frame );

		return true;

	}

	bool read_page_bytes ( pager& owner, uint32_t number, uint8_t* out ) {

		page_frame* frame = get_page ( owner, number );

		if ( !frame )
			return false;

		base::copy ( out, frame->data, PAGE_SIZE );
		release_page ( frame );

		return true;

	}

}
