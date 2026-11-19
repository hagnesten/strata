#pragma once

#include "studio/grid/grid.hh"

#define CHART_BARS 120
#define CHART_POINTS 4000

namespace studio {

	struct studio_state;

	struct chart_state {

		int32_t label_column;
		int32_t value_column;
		bool lines;

	};

	void reset_chart ( chart_state& chart, const grid_data& grid );

	void draw_chart ( ui_context& ui, chart_state& chart, const grid_data& grid, const ui::rect& area );

}