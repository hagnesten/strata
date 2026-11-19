#include "api/api.hh"
#include "base/text/unicode.hh"

namespace api {

	static void link ( strata_db* db, strata_stmt* stmt ) {

		stmt->next = db->statements;

		if ( db->statements )
			db->statements->previous = stmt;

		db->statements = stmt;

	}

	static void unlink ( strata_stmt* stmt ) {

		if ( stmt->previous )
			stmt->previous->next = stmt->next;
		else
			stmt->owner->statements = stmt->next;

		if ( stmt->next )
			stmt->next->previous = stmt->previous;

	}

	static int bind_slot ( strata_stmt* stmt, int index, storage::value*& slot ) {

		if ( !stmt )
			return STRATA_MISUSE;

		if ( stmt->state == API_RUNNING )
			return fail_misuse ( stmt->owner, "reset the statement before binding new values" );

		if ( index < 1 || static_cast < uint32_t > ( index ) > stmt->bound_count )
			return STRATA_RANGE;

		slot = &stmt->bound [ index - 1 ];

		return STRATA_OK;

	}

	static uint8_t* owned_room ( strata_stmt* stmt, int index, size_t size ) {

		api_owned& owned = stmt->owned [ index - 1 ];

		if ( owned.room < size + 1 ) {

			size_t room = base::max < size_t > ( size + 1, owned.room * 2 );
			uint8_t* made = static_cast < uint8_t* > ( base::allocate ( room ) );

			if ( !made )
				return nullptr;

			base::release ( owned.bytes );
			owned.bytes = made;
			owned.room = room;

		}

		return owned.bytes;

	}

	static int bind_bytes ( strata_stmt* stmt, int index, const void* value, size_t size, bool text ) {

		storage::value* slot = nullptr;
		int code = bind_slot ( stmt, index, slot );

		if ( code != STRATA_OK )
			return code;

		if ( !value ) {

			*slot = storage::null_value ( );

			return STRATA_OK;

		}

		uint8_t* kept = owned_room ( stmt, index, size );

		if ( !kept )
			return STRATA_NOMEM;

		if ( size )
			base::copy ( kept, value, size );

		kept [ size ] = 0;
		*slot = text ? storage::text_value ( reinterpret_cast < const char* > ( kept ), size ) : storage::blob_value ( kept, size );

		return STRATA_OK;

	}

}

extern "C" {

	STRATA_API int strata_prepare ( strata_db* db, const char* sql, int size, strata_stmt** out, const char** tail ) {

		if ( out )
			*out = nullptr;

		if ( tail )
			*tail = sql;

		if ( !db || !sql || !out )
			return api::fail_misuse ( db, "strata_prepare needs a database, SQL and a place for the statement" );

		base::text copy;
		const char* text = sql;

		if ( size >= 0 ) {

			copy.add ( sql, static_cast < size_t > ( size ) );
			text = copy.value ( );

		}

		engine::prepared_statement* prepared = nullptr;
		size_t used = 0;

		if ( !engine::prepare_statement ( db->database, text, prepared, &used ) )
			return api::code_of ( db, false );

		if ( tail )
			*tail = sql + used;

		if ( !prepared )
			return STRATA_OK;

		strata_stmt* made = base::make < strata_stmt > ( 1 );
		uint32_t count = engine::statement_parameters ( *prepared );

		if ( made ) {

			made->bound = base::make < storage::value > ( count + 1 );
			made->owned = base::make < api_owned > ( count + 1 );

		}

		if ( !made || !made->bound || !made->owned ) {

			if ( made ) {

				base::release ( made->bound );
				base::release ( made->owned );
				base::release ( made );

			}

			engine::finish_statement ( prepared );

			return STRATA_NOMEM;

		}

		made->owner = db;
		made->prepared = prepared;
		made->bound_count = count;
		made->returns_rows = engine::statement_returns_rows ( *prepared );

		for ( uint32_t i = 0; i < count; i++ )
			made->bound [ i ] = storage::null_value ( );

		api::link ( db, made );
		*out = made;

		return STRATA_OK;

	}

	STRATA_API int strata_finalize ( strata_stmt* stmt ) {

		if ( !stmt )
			return STRATA_OK;

		api::stop_running ( stmt );
		api::unlink ( stmt );
		engine::finish_statement ( stmt->prepared );

		for ( uint32_t i = 0; i < stmt->bound_count; i++ )
			base::release ( stmt->owned [ i ].bytes );

		for ( uint32_t i = 0; i < stmt->text_count; i++ )
			stmt->texts [ i ].~text ( );

		base::release ( stmt->texts );
		base::release ( stmt->row_values );
		base::release ( stmt->owned );
		base::release ( stmt->bound );
		stmt->fetch_memory.reset ( );
		stmt->scratch.reset ( );
		base::release ( stmt );

		return STRATA_OK;

	}

	STRATA_API int strata_reset ( strata_stmt* stmt ) {

		if ( !stmt )
			return STRATA_MISUSE;

		int result = stmt->state == API_DONE ? stmt->result : STRATA_OK;

		api::stop_running ( stmt );
		stmt->state = API_IDLE;
		stmt->row = nullptr;

		return result == STRATA_DONE ? STRATA_OK : result;

	}

	STRATA_API int strata_clear_bindings ( strata_stmt* stmt ) {

		if ( !stmt )
			return STRATA_MISUSE;

		for ( uint32_t i = 0; i < stmt->bound_count; i++ )
			stmt->bound [ i ] = storage::null_value ( );

		return STRATA_OK;

	}

	STRATA_API int strata_parameter_count ( strata_stmt* stmt ) {

		return stmt ? static_cast < int > ( stmt->bound_count ) : 0;

	}

	STRATA_API int strata_parameter_index ( strata_stmt* stmt, const char* name ) {

		if ( !stmt || !name || !stmt->prepared->statement || !stmt->prepared->statement->parameter_names )
			return 0;

		const char* const* names = stmt->prepared->statement->parameter_names;

		for ( uint32_t i = 0; i < stmt->bound_count; i++ )
			if ( names [ i ] && ( base::same ( names [ i ], name ) || ( name [ 0 ] != ':' && name [ 0 ] != '@' && name [ 0 ] != '$' && base::same ( names [ i ] + 1, name ) ) ) )
				return static_cast < int > ( i + 1 );

		return 0;

	}

	STRATA_API const char* strata_parameter_name ( strata_stmt* stmt, int index ) {

		if ( !stmt || index < 1 || static_cast < uint32_t > ( index ) > stmt->bound_count || !stmt->prepared->statement || !stmt->prepared->statement->parameter_names )
			return nullptr;

		return stmt->prepared->statement->parameter_names [ index - 1 ];

	}

	STRATA_API int strata_bind_null ( strata_stmt* stmt, int index ) {

		storage::value* slot = nullptr;
		int code = api::bind_slot ( stmt, index, slot );

		if ( code == STRATA_OK )
			*slot = storage::null_value ( );

		return code;

	}

	STRATA_API int strata_bind_int64 ( strata_stmt* stmt, int index, int64_t value ) {

		storage::value* slot = nullptr;
		int code = api::bind_slot ( stmt, index, slot );

		if ( code == STRATA_OK )
			*slot = storage::integer_value ( value );

		return code;

	}

	STRATA_API int strata_bind_double ( strata_stmt* stmt, int index, double value ) {

		storage::value* slot = nullptr;
		int code = api::bind_slot ( stmt, index, slot );

		if ( code == STRATA_OK )
			*slot = storage::real_value ( value );

		return code;

	}

	STRATA_API int strata_bind_text ( strata_stmt* stmt, int index, const char* value, int size ) {

		return api::bind_bytes ( stmt, index, value, value ? ( size < 0 ? base::length ( value ) : static_cast < size_t > ( size ) ) : 0, true );

	}

	STRATA_API int strata_bind_text16 ( strata_stmt* stmt, int index, const uint16_t* value, int units ) {

		storage::value* slot = nullptr;
		int code = api::bind_slot ( stmt, index, slot );

		if ( code != STRATA_OK )
			return code;

		if ( !value ) {

			*slot = storage::null_value ( );

			return STRATA_OK;

		}

		size_t count = 0;

		if ( units >= 0 )
			count = static_cast < size_t > ( units );
		else
			while ( value [ count ] )
				count++;

		uint8_t* kept = api::owned_room ( stmt, index, base::utf8_room ( count ) );

		if ( !kept )
			return STRATA_NOMEM;

		size_t size = base::utf8_from_utf16 ( value, count, reinterpret_cast < char* > ( kept ) );

		*slot = storage::text_value ( reinterpret_cast < const char* > ( kept ), size );

		return STRATA_OK;

	}

	STRATA_API int strata_bind_blob ( strata_stmt* stmt, int index, const void* value, int size ) {

		return api::bind_bytes ( stmt, index, value, size > 0 ? static_cast < size_t > ( size ) : 0, false );

	}

	STRATA_API int strata_bind_values ( strata_stmt* stmt, const strata_value* values, int count ) {

		if ( !stmt || ( count && !values ) || count < 0 || static_cast < uint32_t > ( count ) > stmt->bound_count )
			return STRATA_MISUSE;

		for ( int i = 0; i < count; i++ ) {

			const strata_value& item = values [ i ];
			int code = STRATA_OK;

			switch ( item.type ) {

				case STRATA_INTEGER:
					code = strata_bind_int64 ( stmt, i + 1, item.data.integer );
					break;

				case STRATA_REAL:
					code = strata_bind_double ( stmt, i + 1, item.data.real );
					break;

				case STRATA_TEXT:
					code = strata_bind_text ( stmt, i + 1, static_cast < const char* > ( item.data.bytes ), static_cast < int > ( item.size ) );
					break;

				case STRATA_TEXT16:
					code = strata_bind_text16 ( stmt, i + 1, static_cast < const uint16_t* > ( item.data.bytes ), static_cast < int > ( item.size / 2 ) );
					break;

				case STRATA_BLOB:
					code = strata_bind_blob ( stmt, i + 1, item.data.bytes, static_cast < int > ( item.size ) );
					break;

				default:
					code = strata_bind_null ( stmt, i + 1 );
					break;

			}

			if ( code != STRATA_OK )
				return code;

		}

		return STRATA_OK;

	}

}
