#include "api/api.hh"

namespace api {

	static bool on_columns ( void* context, const char* const* names, uint32_t count ) {

		strata_stmt* stmt = static_cast < strata_stmt* > ( context );

		stmt->names = names;
		stmt->name_count = count;
		stmt->described = true;

		return true;

	}

	static bool on_row ( void* context, const storage::value* values, uint32_t count ) {

		strata_stmt* stmt = static_cast < strata_stmt* > ( context );

		stmt->width = count;

		if ( stmt->fetching ) {

			if ( !keep_row ( stmt, values, count ) ) {

				stmt->fetch_failed = true;
				base::leave_coroutine ( stmt->runner );

				return false;

			}

			if ( stmt->fetch_count < stmt->fetch_room )
				return !stmt->stopping;

			base::leave_coroutine ( stmt->runner );

			return !stmt->stopping;

		}

		stmt->row = values;
		base::leave_coroutine ( stmt->runner );
		stmt->row = nullptr;

		return !stmt->stopping;

	}

	static void run_rows ( void* context ) {

		strata_stmt* stmt = static_cast < strata_stmt* > ( context );
		engine::query_sink sink = { stmt, on_columns, on_row };
		bool fine = engine::run_prepared ( *stmt->prepared, stmt->bound, stmt->bound_count, &sink );

		stmt->row = nullptr;
		stmt->result = fine ? STRATA_DONE : code_of ( stmt->owner, false );

	}

	base::coroutine* take_runner ( strata_stmt* stmt ) {

		strata_db* db = stmt->owner;
		base::coroutine* runner = db->spare_count ? db->spare [ --db->spare_count ] : base::make_coroutine ( run_rows, stmt );

		if ( runner )
			runner->context = stmt;

		return runner;

	}

	void return_runner ( strata_stmt* stmt ) {

		strata_db* db = stmt->owner;

		if ( !stmt->runner )
			return;

		if ( db->spare_count < API_SPARE_RUNNERS )
			db->spare [ db->spare_count++ ] = stmt->runner;
		else
			base::free_coroutine ( stmt->runner );

		stmt->runner = nullptr;

	}

	void stop_running ( strata_stmt* stmt ) {

		if ( stmt->state == API_RUNNING && stmt->runner ) {

			stmt->stopping = true;

			while ( !stmt->runner->finished )
				base::enter_coroutine ( stmt->runner );

			stmt->stopping = false;

		}

		return_runner ( stmt );

		if ( stmt->state == API_RUNNING )
			stmt->state = API_DONE;

	}

	bool describe ( strata_stmt* stmt ) {

		if ( stmt->described )
			return true;

		const char* const* names = nullptr;
		uint32_t count = 0;

		if ( !engine::describe_statement ( *stmt->prepared, names, count ) )
			return false;

		stmt->names = names;
		stmt->name_count = count;
		stmt->described = true;

		return true;

	}

	int step_once ( strata_stmt* stmt ) {

		if ( stmt->state == API_DONE ) {

			stmt->state = API_IDLE;
			stmt->row = nullptr;

		}

		if ( !stmt->returns_rows ) {

			bool fine = engine::run_prepared ( *stmt->prepared, stmt->bound, stmt->bound_count, nullptr );

			stmt->state = API_DONE;
			stmt->result = fine ? STRATA_DONE : code_of ( stmt->owner, false );

			return stmt->result;

		}

		if ( stmt->state == API_IDLE ) {

			stmt->runner = take_runner ( stmt );

			if ( !stmt->runner )
				return STRATA_NOMEM;

			stmt->state = API_RUNNING;
			stmt->result = STRATA_OK;

		}

		base::enter_coroutine ( stmt->runner );

		if ( stmt->runner->finished ) {

			return_runner ( stmt );
			stmt->state = API_DONE;

			return stmt->result;

		}

		return STRATA_ROW;

	}

}

extern "C" {

	STRATA_API int strata_step ( strata_stmt* stmt ) {

		if ( !stmt )
			return STRATA_MISUSE;

		return api::step_once ( stmt );

	}

}
