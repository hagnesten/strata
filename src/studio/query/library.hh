#pragma once

#include "studio/grid/grid.hh"
#include "studio/editor/editor.hh"
#include "studio/widgets/field.hh"

#define STUDIO_TABS 8
#define STUDIO_HISTORY 200
#define STUDIO_SAVED 200
#define LIBRARY_WIDTH 280.0f
#define QUERY_TABS_HEIGHT 32.0f

namespace studio {

	struct studio_state;

	enum result_mode : uint8_t {

		RESULT_NONE,
		RESULT_GRID,
		RESULT_PLAN,
		RESULT_MESSAGE,
		RESULT_CHART,

	};

	struct plan_row {

		int64_t id;
		int64_t parent;
		const char* detail;
		uint32_t depth;

	};

	struct plan_data {

		base::arena memory;
		plan_row* rows;
		uint32_t count;
		uint32_t room;

	};

	struct query_tab {

		editor_state editor;
		grid_data grid;
		plan_data plan;
		result_mode result;
		base::text message;
		base::text sql;
		bool failed;
		double ms;
		uint32_t number;

	};

	struct saved_query {

		const char* name;
		const char* sql;

	};

	enum library_page : uint8_t {

		LIBRARY_HIDDEN,
		LIBRARY_HISTORY,
		LIBRARY_SAVED,

	};

	struct query_library {

		base::arena memory;
		const char** history;
		uint32_t history_count;
		uint32_t history_room;
		saved_query* saved;
		uint32_t saved_count;
		uint32_t saved_room;
		library_page page;
		float scroll;
		text_field save_name;
		bool naming;

	};

	void start_tabs ( studio_state& app );

	void switch_tab ( studio_state& app, uint32_t tab );

	void add_tab ( studio_state& app, const char* sql );

	void close_tab ( studio_state& app, uint32_t tab );

	void draw_query_tabs ( studio_state& app, const ui::rect& area );

	void load_library ( studio_state& app );

	void note_history ( studio_state& app, const char* sql, size_t size );

	void save_query ( studio_state& app, const char* name, const char* sql, size_t size );

	void draw_library ( studio_state& app, const ui::rect& area );

	void suggest_schema ( void* context, const char* qualifier, size_t qualifier_size, const char* prefix, size_t prefix_size, completion_list& out );

}