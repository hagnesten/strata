#pragma once

#include "studio/widgets/widgets.hh"

#define EDITOR_UNDO 64
#define EDITOR_TAB 4

namespace studio {

	enum token_shade : uint8_t {

		SHADE_PLAIN,
		SHADE_KEYWORD,
		SHADE_STRING,
		SHADE_NUMBER,
		SHADE_COMMENT,
		SHADE_FUNCTION,
		SHADE_PUNCT,

	};

	struct editor_state {

		base::text text;
		size_t caret;
		size_t anchor;
		float scroll_x;
		float scroll_y;
		bool dragging;
		float wanted_x;
		bool wanted;
		size_t* lines;
		uint32_t line_count;
		uint32_t line_room;
		uint8_t* shades;
		size_t shade_room;
		bool dirty;
		base::text undo [ EDITOR_UNDO ];
		size_t undo_caret [ EDITOR_UNDO ];
		uint32_t undo_count;
		uint32_t redo_count;
		bool typing;
		size_t typed_at;
		uint64_t id;
		int64_t marked;

	};

	void set_editor_text ( editor_state& editor, const char* value );

	bool draw_editor ( ui_context& ui, editor_state& editor, const ui::rect& area );

	void editor_selection ( const editor_state& editor, size_t& from, size_t& to );

	void editor_run_text ( const editor_state& editor, base::text& out, size_t& base_offset );

	void editor_place ( editor_state& editor, uint32_t line, uint32_t column );

}
