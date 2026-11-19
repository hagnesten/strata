#include "api/api.hh"
#include "engine/value/value.hh"

namespace api {

	void to_public ( const storage::value& item, strata_value& out ) {

		out = { };

		switch ( item.kind ) {

			case storage::VALUE_INTEGER:
				out.type = STRATA_INTEGER;
				out.data.integer = item.integer;
				break;

			case storage::VALUE_REAL:
				out.type = STRATA_REAL;
				out.data.real = item.real;
				break;

			case storage::VALUE_TEXT:
			case storage::VALUE_BLOB:
				out.type = item.kind == storage::VALUE_TEXT ? STRATA_TEXT : STRATA_BLOB;
				out.size = static_cast < uint32_t > ( item.size );
				out.data.bytes = item.bytes;
				break;

			default:
				out.type = STRATA_NULL;
				break;

		}

	}

	static const storage::value* cell ( strata_stmt* stmt, int column ) {

		if ( !stmt || !stmt->row || column < 0 || static_cast < uint32_t > ( column ) >= stmt->width )
			return nullptr;

		return &stmt->row [ column ];

	}

	static storage::value numeric ( strata_stmt* stmt, const storage::value& item ) {

		if ( item.kind != storage::VALUE_TEXT && item.kind != storage::VALUE_BLOB )
			return item;

		stmt->scratch.recycle ( );

		storage::value text = storage::text_value ( reinterpret_cast < const char* > ( item.bytes ), item.size );
		storage::value number = engine::apply_affinity ( stmt->scratch, text, sql::AFFINITY_NUMERIC );

		return number.kind == storage::VALUE_INTEGER || number.kind == storage::VALUE_REAL ? number : storage::integer_value ( 0 );

	}

	static base::text* text_slot ( strata_stmt* stmt, int column ) {

		uint32_t wanted = static_cast < uint32_t > ( column ) + 1;

		if ( wanted > stmt->text_count ) {

			uint32_t room = base::max < uint32_t > ( wanted, stmt->width );
			base::text* made = base::make < base::text > ( room );

			if ( !made )
				return nullptr;

			if ( stmt->texts )
				base::copy ( made, stmt->texts, sizeof ( base::text ) * stmt->text_count );

			base::release ( stmt->texts );
			stmt->texts = made;
			stmt->text_count = room;

		}

		return &stmt->texts [ column ];

	}

	static const char* text_of ( strata_stmt* stmt, int column, size_t& size ) {

		const storage::value* item = cell ( stmt, column );

		size = 0;

		if ( !item || item->kind == storage::VALUE_NULL )
			return nullptr;

		base::text* slot = text_slot ( stmt, column );

		if ( !slot )
			return nullptr;

		slot->clear ( );

		if ( item->kind == storage::VALUE_TEXT || item->kind == storage::VALUE_BLOB )
			slot->add ( reinterpret_cast < const char* > ( item->bytes ), item->size );
		else
			engine::describe_value ( *slot, *item );

		size = slot->size;

		return slot->value ( );

	}

}

extern "C" {

	STRATA_API int strata_column_count ( strata_stmt* stmt ) {

		if ( !stmt )
			return 0;

		if ( stmt->row )
			return static_cast < int > ( stmt->width );

		return api::describe ( stmt ) ? static_cast < int > ( stmt->name_count ) : 0;

	}

	STRATA_API const char* strata_column_name ( strata_stmt* stmt, int column ) {

		if ( !stmt || !api::describe ( stmt ) || column < 0 || static_cast < uint32_t > ( column ) >= stmt->name_count )
			return nullptr;

		return stmt->names [ column ];

	}

	STRATA_API int strata_column_type ( strata_stmt* stmt, int column ) {

		const storage::value* item = api::cell ( stmt, column );

		if ( !item )
			return STRATA_NULL;

		strata_value out;

		api::to_public ( *item, out );

		return out.type;

	}

	STRATA_API int64_t strata_column_int64 ( strata_stmt* stmt, int column ) {

		const storage::value* item = api::cell ( stmt, column );

		if ( !item )
			return 0;

		storage::value number = api::numeric ( stmt, *item );

		if ( number.kind == storage::VALUE_INTEGER )
			return number.integer;

		if ( number.kind == storage::VALUE_REAL )
			return number.real >= 9.2233720368547758e18 ? 0x7FFFFFFFFFFFFFFFll : number.real <= -9.2233720368547758e18 ? static_cast < int64_t > ( 0x8000000000000000ull ) : static_cast < int64_t > ( number.real );

		return 0;

	}

	STRATA_API double strata_column_double ( strata_stmt* stmt, int column ) {

		const storage::value* item = api::cell ( stmt, column );

		if ( !item )
			return 0;

		storage::value number = api::numeric ( stmt, *item );

		if ( number.kind == storage::VALUE_INTEGER )
			return static_cast < double > ( number.integer );

		return number.kind == storage::VALUE_REAL ? number.real : 0;

	}

	STRATA_API const char* strata_column_text ( strata_stmt* stmt, int column ) {

		size_t size = 0;

		return api::text_of ( stmt, column, size );

	}

	STRATA_API const void* strata_column_blob ( strata_stmt* stmt, int column ) {

		const storage::value* item = api::cell ( stmt, column );

		if ( item && item->kind == storage::VALUE_BLOB )
			return item->bytes;

		size_t size = 0;

		return api::text_of ( stmt, column, size );

	}

	STRATA_API int strata_column_bytes ( strata_stmt* stmt, int column ) {

		const storage::value* item = api::cell ( stmt, column );

		if ( !item || item->kind == storage::VALUE_NULL )
			return 0;

		if ( item->kind == storage::VALUE_TEXT || item->kind == storage::VALUE_BLOB )
			return static_cast < int > ( item->size );

		size_t size = 0;

		api::text_of ( stmt, column, size );

		return static_cast < int > ( size );

	}

	STRATA_API const strata_value* strata_row ( strata_stmt* stmt ) {

		if ( !stmt || !stmt->row )
			return nullptr;

		if ( stmt->row_room < stmt->width ) {

			strata_value* made = base::make < strata_value > ( stmt->width );

			if ( !made )
				return nullptr;

			base::release ( stmt->row_values );
			stmt->row_values = made;
			stmt->row_room = stmt->width;

		}

		for ( uint32_t c = 0; c < stmt->width; c++ )
			api::to_public ( stmt->row [ c ], stmt->row_values [ c ] );

		return stmt->row_values;

	}

}
