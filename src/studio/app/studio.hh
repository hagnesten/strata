#pragma once

#include "engine/database/database.hh"
#include "engine/check/check.hh"
#include "studio/grid/grid.hh"
#include "studio/editor/editor.hh"
#include "studio/widgets/field.hh"
#include "studio/widgets/icons.hh"
#include "studio/files/files.hh"
#include "studio/query/library.hh"
#include "studio/editing/editing.hh"
#include "studio/transfer/transfer.hh"
#include "studio/chart/chart.hh"
#include "studio/designer/designer.hh"

#define STUDIO_TREES 9
#define STUDIO_CAPTURE_WIDTH 1440
#define STUDIO_CAPTURE_HEIGHT 900
#define STUDIO_CARD_WIDTH 228.0f
#define STUDIO_CARD_ROW 22.0f
#define STUDIO_PAGE_CELL 11.0f
#define STUDIO_HEX_ROW 19.0f
#define STUDIO_DATA_LIMIT 20000
#define STUDIO_VERSION "Strata 0.6"

namespace studio {

	enum view_kind : uint8_t {

		VIEW_DATA,
		VIEW_QUERY,
		VIEW_MAP,
		VIEW_PAGES,

	};

	enum map_mode : uint8_t {

		MAP_SCHEMA,
		MAP_STORAGE,

	};

	enum overlay_kind : uint8_t {

		OVERLAY_NONE,
		OVERLAY_OPEN,
		OVERLAY_TRANSFER,
		OVERLAY_DESIGN,
		OVERLAY_CHECK,

	};

	enum window_button : uint8_t {

		WINDOW_NONE,
		WINDOW_MINIMIZE,
		WINDOW_MAXIMIZE,
		WINDOW_CLOSE,

	};

	struct table_stats {

		bool known;
		uint64_t rows;
		uint32_t pages;
		uint32_t leaves;
		uint32_t depth;
		uint32_t overflow;
		uint64_t used;

	};

	struct map_card {

		float x;
		float y;
		bool placed;

	};

	struct tree_node {

		uint32_t page;
		uint8_t kind;
		uint32_t cells;
		uint32_t used;
		uint32_t level;
		int32_t parent;
		uint32_t first_child;
		uint32_t child_count;
		float x;
		float w;
		bool has_rowids;
		int64_t low;
		int64_t high;

	};

	struct tree_view {

		const char* name;
		bool index;
		uint32_t root;
		tree_node* nodes;
		uint32_t count;
		uint32_t levels;
		uint32_t leaves;
		bool clipped;

	};

	struct page_entry {

		uint8_t kind;
		int32_t owner;

	};

	struct header_action {

		icon_kind icon;
		const char* label;
		bool primary;
		bool enabled;

	};

	struct script_check {

		bool fine;
		const char* folder;

	};

	struct studio_state {

		ui::renderer draw;
		input_state input;
		ui_context ui;
		HWND window;
		bool capturing;
		bool maximized;
		window_button window_hot;
		engine::database* db;
		char path [ FILE_PATH_ROOM ];
		const char* file_name;
		view_kind view;
		map_mode map;
		result_mode result;
		overlay_kind overlay;
		int32_t table;
		table_stats stats [ CATALOG_TABLES ];
		grid_data data_grid;
		grid_data result_grid;
		editor_state editor;
		plan_data plan;
		base::text result_message;
		base::text result_sql;
		bool result_failed;
		double result_ms;
		query_tab tabs [ STUDIO_TABS ];
		uint32_t tab_count;
		uint32_t tab;
		uint32_t tab_numbers;
		query_library library;
		cell_edit edit;
		recent_files recent;
		picker_state picker;
		transfer_state transfer;
		chart_state chart;
		design_state design;
		engine::check_report check;
		float check_scroll;
		float header_left;
		base::text status;
		bool status_error;
		float sidebar_width;
		float editor_height;
		float sidebar_scroll;
		map_card cards [ CATALOG_TABLES ];
		bool cards_ready;
		float map_x;
		float map_y;
		int32_t dragging_card;
		float drag_dx;
		float drag_dy;
		bool panning;
		float pan_x;
		float pan_y;
		tree_view trees [ STUDIO_TREES ];
		uint32_t tree_count;
		int32_t trees_for;
		base::arena tree_memory;
		float storage_scroll;
		page_entry* pages;
		uint32_t page_total;
		const char* owners [ CATALOG_TABLES + CATALOG_INDEXES + 1 ];
		uint32_t owner_count;
		bool pages_ready;
		uint32_t page_selected;
		storage::page_summary summary;
		uint8_t page_bytes [ PAGE_SIZE ];
		bool page_loaded;
		float pages_scroll;
		float hex_scroll;
		uint64_t frequency;
		uint32_t surrogate;

	};

	int32_t run_studio ( const char* path );

	int32_t capture_studio ( const char* folder );

	bool make_studio_copy ( );

	studio_state* make_state ( );

	bool prepare_sample ( studio_state& app, const char* path );

	void finish_input ( studio_state& app );

	void draw_frame ( studio_state& app );

	bool open_database_file ( studio_state& app, const char* path );

	void refresh_schema ( studio_state& app );

	void select_table ( studio_state& app, int32_t table );

	void reload_table ( studio_state& app );

	void run_editor ( studio_state& app, bool explain );

	bool run_sql ( studio_state& app, const char* sql, const storage::value* parameters, uint32_t count );

	void after_change ( studio_state& app );

	void set_status ( studio_state& app, const char* message, bool error );

	void ensure_stats ( studio_state& app, int32_t table );

	void ensure_trees ( studio_state& app );

	void ensure_pages ( studio_state& app );

	void select_page ( studio_state& app, uint32_t page );

	void run_check ( studio_state& app );

	void draw_titlebar ( studio_state& app, const ui::rect& area );

	void draw_sidebar ( studio_state& app, const ui::rect& area );

	void draw_status ( studio_state& app, const ui::rect& area );

	int32_t draw_page_header ( studio_state& app, const ui::rect& area, const char* title, const char* subtitle, const header_action* actions, uint32_t count );

	float draw_stat ( studio_state& app, float x, float y, float w, const char* label, const char* value, const char* detail, bool divider );

	ui::rect draw_overlay_frame ( studio_state& app, const ui::rect& area, float width, float height, const char* title );

	void draw_data_view ( studio_state& app, const ui::rect& area );

	void draw_query_view ( studio_state& app, const ui::rect& area );

	void draw_map_view ( studio_state& app, const ui::rect& area );

	void draw_pages_view ( studio_state& app, const ui::rect& area );

	void draw_check_overlay ( studio_state& app, const ui::rect& area );

	void draw_plan ( studio_state& app, const ui::rect& area );

	bool build_sample ( engine::database& db );

	ui::color page_color ( uint8_t kind );

	const char* page_kind_name ( uint8_t kind );

	void size_text ( base::text& out, uint64_t bytes );

	void grouped ( base::text& out, uint64_t value );

	void script_frame ( studio_state& app );

	void script_move ( studio_state& app, float x, float y );

	void script_press ( studio_state& app, float x, float y );

	void script_release ( studio_state& app );

	void script_click ( studio_state& app, float x, float y );

	void script_double_click ( studio_state& app, float x, float y );

	void script_key ( studio_state& app, uint32_t code, bool ctrl, bool shift = false );

	void script_type ( studio_state& app, const char* text );

	void script_wheel ( studio_state& app, float x, float y, float amount );

	bool script_shot ( studio_state& app, const char* folder, const char* name );

	void script_step ( script_check& check, bool passed, const char* what );

	void script_saved ( script_check& check, bool saved );

	bool play_features ( studio_state& app, const char* folder, script_check& check );

}