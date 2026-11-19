#pragma once

#include "studio/widgets/field.hh"

#define DESIGN_COLUMNS 48
#define DESIGN_TYPES 6
#define DESIGN_ROW 40.0f

namespace studio {

	struct studio_state;

	struct design_column {

		text_field name;
		text_field fallback;
		uint8_t type;
		bool primary;
		bool not_null;
		bool unique;
		int32_t original;

	};

	struct design_state {

		text_field name;
		design_column columns [ DESIGN_COLUMNS ];
		uint32_t count;
		int32_t table;
		base::text statements;
		base::text problem;
		bool rebuild;
		float scroll;

	};

	void open_designer ( studio_state& app, int32_t table );

	void draw_designer_overlay ( studio_state& app, const ui::rect& area );

}