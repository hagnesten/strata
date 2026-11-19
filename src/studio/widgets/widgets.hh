#pragma once

#include "ui/render/render.hh"
#include "studio/theme/theme.hh"

#define INPUT_KEYS 32
#define INPUT_CHARS 64

namespace studio {

	enum cursor_kind : uint8_t {

		CURSOR_ARROW,
		CURSOR_TEXT,
		CURSOR_SIZE_WE,
		CURSOR_SIZE_NS,
		CURSOR_HAND,
		CURSOR_MOVE,

	};

	struct input_state {

		float x;
		float y;
		bool down;
		bool pressed;
		bool released;
		bool double_click;
		bool right_pressed;
		float wheel;
		bool shift;
		bool ctrl;
		bool alt;
		uint32_t keys [ INPUT_KEYS ];
		uint32_t key_count;
		uint32_t chars [ INPUT_CHARS ];
		uint32_t char_count;

	};

	struct ui_context {

		ui::renderer* draw;
		input_state* input;
		uint64_t hot;
		uint64_t active;
		uint64_t focus;
		cursor_kind cursor;
		bool redraw;
		float grab;
		bool hover_set;
		base::text hover;

	};

	uint64_t make_id ( const char* name, uint64_t extra = 0 );

	bool hovering ( ui_context& ui, const ui::rect& area );

	bool pressed_in ( ui_context& ui, uint64_t id, const ui::rect& area );

	bool key_hit ( ui_context& ui, uint32_t code );

	bool button ( ui_context& ui, uint64_t id, const ui::rect& area, const char* label, bool primary = false, bool enabled = true );

	bool tab ( ui_context& ui, uint64_t id, const ui::rect& area, const char* label, bool active );

	bool segment ( ui_context& ui, uint64_t id, const ui::rect& area, const char* label, bool active, bool first, bool last );

	void caption ( ui_context& ui, const ui::rect& area, const char* label, const char* right = nullptr );

	void key_value ( ui_context& ui, const ui::rect& area, const char* key, const char* value, ui::color shade );

	bool list_row ( ui_context& ui, uint64_t id, const ui::rect& area, bool selected );

	float scrollbar ( ui_context& ui, uint64_t id, const ui::rect& track, float content, float view, float offset, bool vertical );

	bool splitter ( ui_context& ui, uint64_t id, const ui::rect& area, bool vertical, float& value, float low, float high );

	void hover_text ( ui_context& ui, const char* value );

	float clamp ( float value, float low, float high );

	void table_glyph ( ui::renderer& draw, float x, float y, ui::color shade );

	void index_glyph ( ui::renderer& draw, float x, float y, ui::color shade );

}
