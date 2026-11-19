#include "api/api.hh"
#include "engine/version.hh"

namespace api {

	static bool contains ( const char* text, const char* part ) {

		size_t size = base::length ( part );

		for ( size_t i = 0; text [ i ]; i++ )
			if ( base::same_bytes ( text + i, part, size ) )
				return true;

		return false;

	}

	int code_of ( strata_db* db, bool fine ) {

		if ( fine )
			return STRATA_OK;

		const engine::database& item = db->database;
		const char* problem = item.problem.value ( );

		if ( item.parse_failed )
			return STRATA_ERROR;

		if ( item.pages.error == storage::PAGER_BUSY || contains ( problem, "the database is busy" ) || contains ( problem, "the database changed after" ) )
			return STRATA_BUSY;

		if ( item.pages.error == storage::PAGER_IO )
			return STRATA_IOERR;

		if ( item.pages.error == storage::PAGER_CORRUPT || contains ( problem, "is damaged" ) )
			return STRATA_CORRUPT;

		if ( item.pages.error == storage::PAGER_MEMORY || contains ( problem, "out of memory" ) )
			return STRATA_NOMEM;

		if ( contains ( problem, "constraint failed" ) )
			return STRATA_CONSTRAINT;

		return STRATA_ERROR;

	}

	int fail_misuse ( strata_db* db, const char* what ) {

		if ( db ) {

			db->database.problem.clear ( );
			db->database.problem.add ( what );

		}

		return STRATA_MISUSE;

	}

}

extern "C" {

	STRATA_API const char* strata_version ( void ) {

		return STRATA_VERSION;

	}

	STRATA_API int strata_open ( const char* path, strata_db** out ) {

		if ( !out )
			return STRATA_MISUSE;

		*out = nullptr;

		if ( !path )
			return STRATA_MISUSE;

		strata_db* made = base::make < strata_db > ( 1 );

		if ( !made )
			return STRATA_NOMEM;

		*out = made;

		if ( !engine::open_database ( made->database, path ) )
			return made->database.pages.error == storage::PAGER_CORRUPT ? STRATA_CORRUPT : STRATA_IOERR;

		return STRATA_OK;

	}

	STRATA_API int strata_close ( strata_db* db ) {

		if ( !db )
			return STRATA_OK;

		while ( db->statements )
			strata_finalize ( db->statements );

		for ( uint32_t i = 0; i < db->spare_count; i++ )
			base::free_coroutine ( db->spare [ i ] );

		engine::close_database ( db->database );
		db->~strata_db ( );
		base::release ( db );

		return STRATA_OK;

	}

	STRATA_API int strata_exec ( strata_db* db, const char* sql ) {

		if ( !db || !sql )
			return api::fail_misuse ( db, "strata_exec needs a database and SQL" );

		return api::code_of ( db, engine::execute ( db->database, sql, nullptr, 0, nullptr ) );

	}

	STRATA_API int strata_busy_timeout ( strata_db* db, int milliseconds ) {

		if ( !db )
			return STRATA_MISUSE;

		db->database.pages.wal.busy_milliseconds = milliseconds > 0 ? static_cast < uint32_t > ( milliseconds ) : 0;

		return STRATA_OK;

	}

	STRATA_API const char* strata_error ( strata_db* db ) {

		return db ? db->database.problem.value ( ) : "no database";

	}

	STRATA_API int strata_error_line ( strata_db* db ) {

		return db ? static_cast < int > ( db->database.error_line ) : 0;

	}

	STRATA_API int strata_error_column ( strata_db* db ) {

		return db ? static_cast < int > ( db->database.error_column ) : 0;

	}

	STRATA_API int64_t strata_changes ( strata_db* db ) {

		return db ? db->database.changes : 0;

	}

	STRATA_API int64_t strata_total_changes ( strata_db* db ) {

		return db ? db->database.total_changes : 0;

	}

	STRATA_API int64_t strata_last_rowid ( strata_db* db ) {

		return db ? db->database.last_rowid : 0;

	}

	STRATA_API int strata_in_transaction ( strata_db* db ) {

		return db && db->database.in_transaction ? 1 : 0;

	}

}
