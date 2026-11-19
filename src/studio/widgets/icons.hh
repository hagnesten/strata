#pragma once

#include "studio/widgets/widgets.hh"

#define ICON_SIZE 16.0f

namespace studio {

	enum icon_kind : uint8_t {

		ICON_DATA,
		ICON_QUERY,
		ICON_MAP,
		ICON_PAGES,
		ICON_TABLE,
		ICON_INDEX,
		ICON_PLAY,
		ICON_PLAN,
		ICON_SAVE,
		ICON_HISTORY,
		ICON_OPEN,
		ICON_IMPORT,
		ICON_EXPORT,
		ICON_PLUS,
		ICON_TRASH,
		ICON_EDIT,
		ICON_CHECK,
		ICON_CHART,
		ICON_REFRESH,
		ICON_CLOSE,
		ICON_MINIMIZE,
		ICON_MAXIMIZE,
		ICON_RESTORE,
		ICON_LOGO,
		ICON_FILE,
		ICON_UP,
		ICON_DRIVE,
		ICON_KEY,
		ICON_ROWS,

	};

	void draw_icon ( ui::renderer& draw, icon_kind kind, float x, float y, ui::color shade );

	float icon_button_width ( ui::renderer& draw, const char* label );

	bool icon_button ( ui_context& ui, uint64_t id, const ui::rect& area, icon_kind icon, const char* label, bool primary = false, bool enabled = true );

	bool icon_only ( ui_context& ui, uint64_t id, const ui::rect& area, icon_kind icon, const char* tip, bool enabled = true );

}