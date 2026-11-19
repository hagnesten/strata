#include "storage/btree/btree.hh"

#define COPY_COMMIT_ROWS 4096

namespace storage {

	bool copy_btree ( pager& from, uint32_t root, bool index, pager& to, uint32_t& made ) {

		if ( !create_btree ( to, index, made ) )
			return false;

		btree source = { &from, root, index };
		btree target = { &to, made, index };
		cursor at;
		base::text payload;
		uint32_t copied = 0;

		if ( !cursor_first ( at, source ) )
			return false;

		while ( at.valid ) {

			bool existed = false;
			int64_t rowid = 0;

			if ( !cursor_payload ( at, payload ) )
				return false;

			const uint8_t* bytes = reinterpret_cast < const uint8_t* > ( payload.value ( ) );

			if ( index ? !index_insert ( target, bytes, payload.size, existed ) : !cursor_rowid ( at, rowid ) || !table_insert ( target, rowid, bytes, payload.size, false, existed ) )
				return false;

			if ( !cursor_next ( at ) )
				return false;

			if ( ++copied % COPY_COMMIT_ROWS == 0 && !commit_pager ( to ) )
				return false;

		}

		return true;

	}

}