#include "api/api.hh"
#include "base/text/unicode.hh"

namespace api {

	bool from_public ( base::arena& memory, const strata_value& item, storage::value& out ) {

		switch ( item.type ) {

			case STRATA_INTEGER:
				out = storage::integer_value ( item.data.integer );
				return true;

			case STRATA_REAL:
				out = storage::real_value ( item.data.real );
				return true;

			case STRATA_TEXT:
				out = item.data.bytes ? storage::text_value ( static_cast < const char* > ( item.data.bytes ), item.size ) : storage::null_value ( );
				return true;

			case STRATA_BLOB:
				out = item.data.bytes ? storage::blob_value ( static_cast < const uint8_t* > ( item.data.bytes ), item.size ) : storage::null_value ( );
				return true;

			case STRATA_TEXT16: {

				if ( !item.data.bytes ) {

					out = storage::null_value ( );

					return true;

				}

				size_t units = item.size / 2;
				char* made = static_cast < char* > ( memory.take_raw ( base::utf8_room ( units ) ) );

				if ( !made )
					return false;

				size_t size = base::utf8_from_utf16 ( static_cast < const uint16_t* > ( item.data.bytes ), units, made );

				out = storage::text_value ( made, size );

				return true;

			}

			default:
				out = storage::null_value ( );
				return true;

		}

	}

	static bool keep_public ( base::arena& memory, strata_value& item, bool wide ) {

		if ( ( item.type != STRATA_TEXT && item.type != STRATA_BLOB ) || !item.data.bytes )
			return true;

		if ( wide && item.type == STRATA_TEXT ) {

			uint16_t* units = static_cast < uint16_t* > ( memory.take_raw ( static_cast < size_t > ( item.size ) * 2 + 2 ) );

			if ( !units )
				return false;

			size_t count = base::utf16_from_utf8 ( static_cast < const uint8_t* > ( item.data.bytes ), item.size, units );

			item.type = STRATA_TEXT16;
			item.size = static_cast < uint32_t > ( count * 2 );
			item.data.bytes = units;

			return true;

		}

		char* kept = memory.keep ( static_cast < const char* > ( item.data.bytes ), item.size );

		if ( !kept )
			return false;

		item.data.bytes = kept;

		return true;

	}

	bool keep_row ( strata_stmt* stmt, const storage::value* values, uint32_t count ) {

		strata_value* at = stmt->fetch_out + static_cast < size_t > ( stmt->fetch_count ) * count;

		for ( uint32_t c = 0; c < count; c++ ) {

			to_public ( values [ c ], at [ c ] );

			if ( !keep_public ( stmt->fetch_memory, at [ c ], stmt->fetch_wide ) )
				return false;

		}

		stmt->fetch_count++;

		return true;

	}

}

namespace api {

	static int fetch_rows ( strata_stmt* stmt, strata_value* out, int room_rows, int* rows, bool wide ) {

		if ( rows )
			*rows = 0;

		if ( !stmt || !out || room_rows <= 0 )
			return STRATA_MISUSE;

		stmt->fetch_wide = wide;
		stmt->fetch_memory.recycle ( );
		stmt->fetch_out = out;
		stmt->fetch_room = room_rows;
		stmt->fetch_count = 0;
		stmt->fetch_failed = false;
		stmt->fetching = true;

		int code = api::step_once ( stmt );

		stmt->fetching = false;

		if ( rows )
			*rows = stmt->fetch_count;

		if ( stmt->fetch_failed )
			return STRATA_NOMEM;

		return code;

	}

}

extern "C" {

	STRATA_API int strata_fetch ( strata_stmt* stmt, strata_value* out, int room_rows, int* rows ) {

		return api::fetch_rows ( stmt, out, room_rows, rows, false );

	}

	STRATA_API int strata_fetch16 ( strata_stmt* stmt, strata_value* out, int room_rows, int* rows ) {

		return api::fetch_rows ( stmt, out, room_rows, rows, true );

	}

	STRATA_API int strata_run_batch ( strata_stmt* stmt, const strata_value* values, int rows, int width, int* done ) {

		if ( done )
			*done = 0;

		if ( !stmt || rows < 0 || width < 0 || ( rows && width && !values ) || static_cast < uint32_t > ( width ) > stmt->bound_count )
			return STRATA_MISUSE;

		if ( stmt->state == API_RUNNING )
			return api::fail_misuse ( stmt->owner, "reset the statement before running a batch" );

		storage::value* row = stmt->bound;

		for ( int r = 0; r < rows; r++ ) {

			const strata_value* given = values + static_cast < size_t > ( r ) * width;

			stmt->scratch.recycle ( );

			for ( int c = 0; c < width; c++ )
				if ( !api::from_public ( stmt->scratch, given [ c ], row [ c ] ) )
					return STRATA_NOMEM;

			if ( !engine::run_prepared ( *stmt->prepared, row, stmt->bound_count, nullptr ) ) {

				for ( int c = 0; c < width; c++ )
					row [ c ] = storage::null_value ( );

				return api::code_of ( stmt->owner, false );

			}

			if ( done )
				*done = r + 1;

		}

		for ( int c = 0; c < width; c++ )
			row [ c ] = storage::null_value ( );

		stmt->state = API_DONE;
		stmt->result = STRATA_DONE;

		return STRATA_OK;

	}

}
