#include "storage/pager/pager.hh"

namespace storage {

	static void forget_mark ( pager_mark& mark ) {

		for ( uint32_t i = 0; i < mark.copy_count; i++ )
			base::release ( mark.copies [ i ].data );

		base::release ( mark.copies );
		base::fill ( &mark, 0, sizeof ( mark ) );

	}

	static bool reserve_copies ( pager_mark& mark, uint32_t wanted ) {

		if ( wanted > mark.copy_room ) {

			uint32_t grown = mark.copy_room ? mark.copy_room : 64;

			while ( grown < wanted )
				grown *= 2;

			page_copy* made = base::make < page_copy > ( grown );

			if ( !made )
				return false;

			if ( mark.copies ) {

				base::copy ( made, mark.copies, sizeof ( page_copy ) * mark.copy_count );
				base::release ( mark.copies );

			}

			mark.copies = made;
			mark.copy_room = grown;

		}

		return true;

	}

	static bool add_copy ( pager_mark& mark, page_frame* frame, uint8_t* data ) {

		if ( !reserve_copies ( mark, mark.copy_count + 1 ) )
			return false;

		mark.copies [ mark.copy_count++ ] = { frame, data };

		return true;

	}

	bool push_mark ( pager& item ) {

		mark_stack& stack = item.marks;

		if ( stack.count == stack.room ) {

			uint32_t grown = stack.room ? stack.room * 2 : 8;
			pager_mark* made = base::make < pager_mark > ( grown );

			if ( !made ) {

				fail ( item, PAGER_MEMORY, "out of memory while marking the transaction", 0, nullptr );

				return false;

			}

			if ( stack.marks ) {

				base::copy ( made, stack.marks, sizeof ( pager_mark ) * stack.count );
				base::release ( stack.marks );

			}

			stack.marks = made;
			stack.room = grown;

		}

		pager_mark& mark = stack.marks [ stack.count ];

		base::fill ( &mark, 0, sizeof ( mark ) );
		mark.epoch = ++stack.epoch;
		mark.header = item.header;
		stack.count++;

		return true;

	}

	bool save_for_mark ( pager& item, page_frame* frame ) {

		pager_mark& mark = item.marks.marks [ item.marks.count - 1 ];
		uint8_t* data = base::make < uint8_t > ( PAGE_SIZE );

		if ( !data || !add_copy ( mark, frame, data ) ) {

			base::release ( data );
			fail ( item, PAGER_MEMORY, "out of memory while saving page ", frame->number, " for undo" );

			return false;

		}

		base::copy ( data, frame->data, PAGE_SIZE );
		frame->saved_epoch = mark.epoch;

		return true;

	}

	bool change_page ( pager& item, page_frame* frame ) {

		mark_stack& stack = item.marks;

		if ( !frame->dirty ) {

			frame->dirty = true;
			frame->dirty_epoch = stack.epoch;
			frame->saved_epoch = stack.epoch;

			return true;

		}

		if ( stack.count && frame->saved_epoch < stack.marks [ stack.count - 1 ].epoch )
			return save_for_mark ( item, frame );

		return true;

	}

	static void clear_flags ( pager_mark& mark ) {

		for ( uint32_t i = 0; i < mark.copy_count; i++ )
			mark.copies [ i ].frame->marked = 0;

	}

	static bool release_top ( pager& item ) {

		mark_stack& stack = item.marks;
		pager_mark& top = stack.marks [ stack.count - 1 ];

		if ( stack.count == 1 ) {

			forget_mark ( top );
			stack.count--;

			return true;

		}

		pager_mark& below = stack.marks [ stack.count - 2 ];

		for ( uint32_t i = 0; i < below.copy_count; i++ )
			below.copies [ i ].frame->marked = 1;

		uint32_t moving = 0;

		for ( uint32_t i = 0; i < top.copy_count; i++ )
			if ( !top.copies [ i ].frame->marked && top.copies [ i ].frame->dirty_epoch < below.epoch )
				moving++;

		if ( !reserve_copies ( below, below.copy_count + moving ) ) {

			clear_flags ( below );
			fail ( item, PAGER_MEMORY, "out of memory while folding a savepoint", 0, nullptr );

			return false;

		}

		for ( uint32_t i = 0; i < top.copy_count; i++ ) {

			page_copy& copy = top.copies [ i ];

			if ( !copy.frame->marked && copy.frame->dirty_epoch < below.epoch ) {

				below.copies [ below.copy_count++ ] = copy;
				copy.frame->marked = 1;
				copy.data = nullptr;

			}

			copy.frame->saved_epoch = below.epoch;

		}

		clear_flags ( below );
		forget_mark ( top );
		stack.count--;

		return true;

	}

	bool release_mark ( pager& item, uint32_t depth ) {

		while ( item.marks.count > depth )
			if ( !release_top ( item ) )
				return false;

		return true;

	}

	void undo_to_mark ( pager& item, uint32_t depth, bool keep ) {

		mark_stack& stack = item.marks;

		if ( depth >= stack.count )
			return;

		item.shape++;

		for ( uint32_t m = stack.count; m-- > depth; ) {

			pager_mark& mark = stack.marks [ m ];

			for ( uint32_t i = mark.copy_count; i-- > 0; ) {

				base::copy ( mark.copies [ i ].frame->data, mark.copies [ i ].data, PAGE_SIZE );
				mark.copies [ i ].frame->dirty = true;
				mark.copies [ i ].frame->saved_epoch = 0;

			}

		}

		pager_mark& target = stack.marks [ depth ];

		for ( uint32_t i = 0; i < item.frame_count; i++ ) {

			page_frame* frame = item.frames [ i ];

			if ( frame->number && frame->dirty && frame->dirty_epoch >= target.epoch )
				discard_frame ( item, frame );

		}

		item.header = target.header;
		item.error = PAGER_FINE;
		item.problem [ 0 ] = 0;

		while ( stack.count > depth + 1 )
			forget_mark ( stack.marks [ --stack.count ] );

		if ( keep ) {

			for ( uint32_t i = 0; i < target.copy_count; i++ )
				base::release ( target.copies [ i ].data );

			target.copy_count = 0;
			target.epoch = ++stack.epoch;

		} else
			forget_mark ( stack.marks [ --stack.count ] );

	}

	void clear_marks ( pager& item ) {

		while ( item.marks.count )
			forget_mark ( item.marks.marks [ --item.marks.count ] );

		base::release ( item.marks.marks );
		item.marks.marks = nullptr;
		item.marks.room = 0;

	}

	bool begin_statement ( pager& item ) {

		return push_mark ( item );

	}

	bool end_statement ( pager& item ) {

		return !item.marks.count || release_mark ( item, item.marks.count - 1 );

	}

	void undo_statement ( pager& item ) {

		if ( item.marks.count )
			undo_to_mark ( item, item.marks.count - 1, false );

	}

}
