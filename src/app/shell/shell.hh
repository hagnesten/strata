#pragma once

#include "engine/database/database.hh"

#define SHELL_CELL_WIDTH 48

namespace app {

	struct shell_table {

		base::arena memory;
		const char** names;
		uint32_t width;
		const char** cells;
		uint32_t count;
		uint32_t room;
		uint32_t rows;
		bool announced;

	};

	int32_t run_sql ( const char* path, const char* sql );

	int32_t run_shell ( const char* path );

}
