#pragma once

#include "studio/widgets/widgets.hh"

#define EDITOR_UNDO 64
#define EDITOR_TAB 4
#define COMPLETE_ROOM 64
#define COMPLETE_WORD 64
#define COMPLETE_SHOWN 8
#define COMPLETE_ROW 24.0f
#define COMPLETE_WIDTH 300.0f

namespace studio {

	enum completion_kind : uint8_t {

		COMPLETE_TABLE,
		COMPLETE_COLUMN,
		COMPLETE_KEYWORD,
		COMPLETE_FUNCTION,

	};

	struct completion_list {

		char items [ COMPLETE_ROOM ] [ COMPLETE_WORD ];
		char details [ COMPLETE_ROOM ] [ COMPLETE_WORD ];
		uint8_t kinds [ COMPLETE_ROOM ];
		uint32_t count;
		uint32_t selected;
		uint32_t first;
		bool open;
		size_t start;
		float x;
		float y;

	};

	typedef void ( *suggest_words ) ( void* context, const char* qualifier, size_t qualifier_size, const char* prefix, size_t prefix_size, completion_list& out );

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
		completion_list complete;
		suggest_words suggest;
		void* suggest_context;

	};

	void add_completion ( completion_list& list, const char* word, size_t size, completion_kind kind, const char* detail );

	void refresh_completion ( editor_state& editor );

	bool completion_key ( editor_state& editor, uint32_t key );

	void draw_completion ( ui_context& ui, editor_state& editor, const ui::rect& bounds );

	bool completion_click ( ui_context& ui, editor_state& editor, const ui::rect& bounds );

	bool editor_word_char ( char value );

	void editor_replace ( editor_state& editor, size_t from, size_t to, const char* value, size_t size );

	void set_editor_text ( editor_state& editor, const char* value );

	bool draw_editor ( ui_context& ui, editor_state& editor, const ui::rect& area );

	void editor_selection ( const editor_state& editor, size_t& from, size_t& to );

	void editor_run_text ( const editor_state& editor, base::text& out, size_t& base_offset );

	void editor_place ( editor_state& editor, uint32_t line, uint32_t column );

}
