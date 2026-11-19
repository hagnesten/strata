#pragma once

#include "studio/widgets/field.hh"

#define CHANGE_BAR 44.0f

namespace studio {

	struct studio_state;

	struct cell_edit {

		bool active;
		uint32_t row;
		uint32_t column;
		int64_t rowid;
		bool was_null;
		bool focus_pending;
		text_field field;

	};

	void begin_cell_edit ( studio_state& app, uint32_t row, uint32_t column );

	void draw_cell_edit ( studio_state& app, const ui::rect& grid_area );

	bool commit_cell_edit ( studio_state& app, bool null_value );

	void cancel_cell_edit ( studio_state& app );

	void add_table_row ( studio_state& app );

	void delete_table_row ( studio_state& app );

	void finish_changes ( studio_state& app, bool keep );

	void draw_change_bar ( studio_state& app, const ui::rect& area );

	bool row_rowid ( studio_state& app, uint32_t row, int64_t& rowid );

}