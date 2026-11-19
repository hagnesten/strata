#pragma once

#ifndef STRATA_BUILDING
#define STRATA_BUILDING 1
#endif

#include "strata.h"
#include "engine/database/prepared.hh"
#include "base/runtime/coroutine.hh"

#define API_SPARE_RUNNERS 4
#define API_IDLE 0
#define API_RUNNING 1
#define API_DONE 2

struct api_owned {

	uint8_t* bytes;
	size_t room;

};

struct strata_db {

	engine::database database;
	base::coroutine* spare [ API_SPARE_RUNNERS ];
	uint32_t spare_count;
	strata_stmt* statements;

};

struct strata_stmt {

	strata_db* owner;
	engine::prepared_statement* prepared;
	storage::value* bound;
	api_owned* owned;
	uint32_t bound_count;
	base::coroutine* runner;
	int32_t state;
	int32_t result;
	bool stopping;
	bool returns_rows;
	bool described;
	const storage::value* row;
	uint32_t width;
	const char* const* names;
	uint32_t name_count;
	base::text* texts;
	uint32_t text_count;
	strata_value* row_values;
	uint32_t row_room;
	base::arena fetch_memory;
	base::arena scratch;
	strata_value* fetch_out;
	int32_t fetch_room;
	int32_t fetch_count;
	bool fetching;
	bool fetch_failed;
	bool fetch_wide;
	strata_stmt* next;
	strata_stmt* previous;

};

namespace api {

	int code_of ( strata_db* db, bool fine );

	int fail_misuse ( strata_db* db, const char* what );

	base::coroutine* take_runner ( strata_stmt* stmt );

	void return_runner ( strata_stmt* stmt );

	int step_once ( strata_stmt* stmt );

	void stop_running ( strata_stmt* stmt );

	bool describe ( strata_stmt* stmt );

	void to_public ( const storage::value& item, strata_value& out );

	bool from_public ( base::arena& memory, const strata_value& item, storage::value& out );

	bool keep_row ( strata_stmt* stmt, const storage::value* values, uint32_t count );

}
