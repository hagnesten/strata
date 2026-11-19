#pragma once

#include "studio/widgets/widgets.hh"

namespace studio {

	enum field_event : uint8_t {

		FIELD_IDLE,
		FIELD_CHANGED,
		FIELD_SUBMIT,
		FIELD_CANCEL,

	};

	struct text_field {

		base::text text;
		size_t caret;
		size_t anchor;
		float scroll;
		uint64_t id;

	};

	void set_field ( text_field& field, const char* value, size_t size );

	void set_field ( text_field& field, const char* value );

	void focus_field ( ui_context& ui, text_field& field );

	field_event text_box ( ui_context& ui, text_field& field, const ui::rect& area, const char* placeholder, bool code = false );

}