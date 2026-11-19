#include "studio/chart/chart.hh"
#include "studio/widgets/icons.hh"
#include "engine/value/value.hh"

namespace studio {

	static bool cell_number ( const grid_data& grid, uint32_t row, uint32_t column, double& out ) {

		uint8_t kind = grid.kinds [ static_cast < size_t > ( row ) * grid.column_count + column ];

		if ( kind != storage::VALUE_INTEGER && kind != storage::VALUE_REAL )
			return false;

		const char* text = grid_cell ( grid, row, column );
		storage::value value;

		if ( !engine::read_number_text ( reinterpret_cast < const uint8_t* > ( text ), base::length ( text ), value, false ) )
			return false;

		return engine::real_of ( value, out );

	}

	void reset_chart ( chart_state& chart, const grid_data& grid ) {

		chart.label_column = -1;
		chart.value_column = -1;

		for ( uint32_t c = grid.hidden; c < grid.column_count; c++ ) {

			if ( grid.columns [ c ].numeric && chart.value_column < 0 )
				chart.value_column = static_cast < int32_t > ( c );
			else if ( !grid.columns [ c ].numeric && chart.label_column < 0 )
				chart.label_column = static_cast < int32_t > ( c );

		}

		if ( chart.label_column < 0 )
			chart.label_column = static_cast < int32_t > ( grid.hidden );

	}

	static double nice_step ( double span ) {

		double step = 1;

		while ( step * 10 < span )
			step *= 10;

		while ( step > span && step > 1e-9 )
			step /= 10;

		if ( span / step > 8 )
			step *= 5;
		else if ( span / step > 4 )
			step *= 2;

		return step;

	}

	static bool cycle_button ( ui_context& ui, uint64_t id, const ui::rect& area, const char* label, const char* value ) {

		ui::renderer& draw = *ui.draw;
		bool clicked = button ( ui, id, area, "" );
		float label_w = ui::measure ( draw, ui::FONT_SMALL, label );

		ui::text ( draw, ui::FONT_SMALL, label, { area.x + 12, area.y, label_w + 4, area.h }, ui::rgb ( THEME_DIM ) );
		ui::text ( draw, ui::FONT_STRONG, value, { area.x + 20 + label_w, area.y, area.w - label_w - 32, area.h }, ui::rgb ( THEME_TEXT ) );

		return clicked;

	}

	void draw_chart ( ui_context& ui, chart_state& chart, const grid_data& grid, const ui::rect& area ) {

		ui::renderer& draw = *ui.draw;

		ui::fill ( draw, area, ui::rgb ( THEME_BACKGROUND ) );

		if ( chart.value_column < 0 || chart.value_column >= static_cast < int32_t > ( grid.column_count ) || !grid.rows ) {

			ui::text ( draw, ui::FONT_UI, "A chart needs a result with at least one number column", area, ui::rgb ( THEME_FAINT ), ui::ALIGN_CENTER );

			return;

		}

		if ( chart.label_column >= static_cast < int32_t > ( grid.column_count ) )
			chart.label_column = static_cast < int32_t > ( grid.hidden );

		float bar_y = area.y + 12;

		if ( cycle_button ( ui, make_id ( "chart.label" ), { area.x + 28, bar_y, 240, 32 }, "Label", grid.columns [ chart.label_column ].name ) ) {

			chart.label_column = static_cast < int32_t > ( ( static_cast < uint32_t > ( chart.label_column ) + 1 - grid.hidden ) % ( grid.column_count - grid.hidden ) + grid.hidden );

		}

		if ( cycle_button ( ui, make_id ( "chart.value" ), { area.x + 280, bar_y, 240, 32 }, "Value", grid.columns [ chart.value_column ].name ) ) {

			for ( uint32_t step = 1; step <= grid.column_count; step++ ) {

				uint32_t next = ( static_cast < uint32_t > ( chart.value_column ) + step - grid.hidden ) % ( grid.column_count - grid.hidden ) + grid.hidden;

				if ( grid.columns [ next ].numeric ) {

					chart.value_column = static_cast < int32_t > ( next );

					break;

				}

			}

		}

		if ( segment ( ui, make_id ( "chart.bars" ), { area.x + 540, bar_y, 90, 32 }, "Bars", !chart.lines, true, false ) )
			chart.lines = false;

		if ( segment ( ui, make_id ( "chart.line" ), { area.x + 630, bar_y, 90, 32 }, "Line", chart.lines, false, true ) )
			chart.lines = true;

		uint32_t limit = chart.lines ? CHART_POINTS : CHART_BARS;
		uint32_t count = grid.rows < limit ? grid.rows : limit;
		double low = 0;
		double high = 0;
		bool any = false;

		for ( uint32_t r = 0; r < count; r++ ) {

			double value = 0;

			if ( !cell_number ( grid, r, static_cast < uint32_t > ( chart.value_column ), value ) )
				continue;

			low = any ? base::min ( low, value ) : base::min ( 0.0, value );
			high = any ? base::max ( high, value ) : base::max ( 0.0, value );
			any = true;

		}

		ui::rect plot = { area.x + 92, bar_y + 64, area.w - 92 - 40, area.h - ( bar_y - area.y ) - 64 - 52 };

		if ( !any || plot.w < 60 || plot.h < 60 ) {

			ui::text ( draw, ui::FONT_UI, "No numbers to draw in that column", { area.x, bar_y + 44, area.w, area.h - 56 }, ui::rgb ( THEME_FAINT ), ui::ALIGN_CENTER );

			return;

		}

		if ( high == low )
			high = low + 1;

		double step = nice_step ( ( high - low ) / 4 );
		double top = step;

		while ( top < high )
			top += step;

		double bottom = 0;

		while ( bottom > low )
			bottom -= step;

		auto to_y = [ & ] ( double value ) {

			return plot.y + plot.h - static_cast < float > ( ( value - bottom ) / ( top - bottom ) ) * plot.h;

		};

		for ( double mark = bottom; mark <= top + step * 0.5; mark += step ) {

			float y = to_y ( mark );
			base::text label;

			if ( mark == static_cast < double > ( static_cast < int64_t > ( mark ) ) )
				label.signed_number ( static_cast < int64_t > ( mark ) );
			else
				label.real ( mark );

			ui::line ( draw, plot.x, y + 0.5f, plot.x + plot.w, y + 0.5f, ui::rgb ( mark == 0 ? THEME_BORDER : THEME_DIVIDER ) );
			ui::text ( draw, ui::FONT_SMALL, label.value ( ), label.size, { area.x + 10, y - 10, plot.x - area.x - 22, 20 }, ui::rgb ( THEME_FAINT ), ui::ALIGN_RIGHT );

		}

		float slot = plot.w / static_cast < float > ( count );
		float zero = to_y ( 0 );
		int32_t hovered = -1;
		float label_room = ui::measure ( draw, ui::FONT_SMALL, "MMMMMMMM" );
		uint32_t every = static_cast < uint32_t > ( label_room / ( slot > 1 ? slot : 1 ) ) + 1;
		float last_x = 0;
		float last_y = 0;
		bool started = false;

		if ( ui::inside ( plot, ui.input->x, ui.input->y ) )
			hovered = static_cast < int32_t > ( ( ui.input->x - plot.x ) / slot );

		for ( uint32_t r = 0; r < count; r++ ) {

			double value = 0;
			float x = plot.x + slot * static_cast < float > ( r );

			if ( !cell_number ( grid, r, static_cast < uint32_t > ( chart.value_column ), value ) )
				continue;

			float y = to_y ( value );
			bool lit = static_cast < int32_t > ( r ) == hovered;

			if ( chart.lines ) {

				float cx = x + slot * 0.5f;

				if ( started )
					ui::line ( draw, last_x, last_y, cx, y, ui::rgb ( THEME_ACCENT ), 1.6f );

				if ( count <= 80 || lit )
					ui::circle ( draw, cx, y, lit ? 4.0f : 2.5f, ui::rgb ( lit ? THEME_ACCENT_SOFT : THEME_ACCENT ) );

				last_x = cx;
				last_y = y;
				started = true;

			} else {

				float gap = slot > 8 ? slot * 0.22f : 1;
				float top_y = y < zero ? y : zero;

				ui::fill ( draw, { x + gap * 0.5f, top_y, slot - gap, base::max ( 1.0f, y < zero ? zero - y : y - zero ) }, ui::rgb ( lit ? THEME_ACCENT_SOFT : THEME_ACCENT, lit ? 1.0f : 0.85f ) );

			}

			if ( r % every == 0 ) {

				const char* label = grid_cell ( grid, r, static_cast < uint32_t > ( chart.label_column ) );

				ui::text ( draw, ui::FONT_SMALL, label, { x + slot * 0.5f - label_room * 0.5f, plot.y + plot.h + 8, label_room, 20 }, ui::rgb ( THEME_DIM ), ui::ALIGN_CENTER );

			}

			if ( lit ) {

				base::text tip;

				tip.add ( grid_cell ( grid, r, static_cast < uint32_t > ( chart.label_column ) ) ).add ( "   " ).add ( grid.columns [ chart.value_column ].name ).add ( " " ).add ( grid_cell ( grid, r, static_cast < uint32_t > ( chart.value_column ) ) );
				hover_text ( ui, tip.value ( ) );

			}

		}

		if ( grid.rows > count ) {

			base::text note;

			note.add ( "showing the first " ).number ( count ).add ( " rows" );
			ui::text ( draw, ui::FONT_SMALL, note.value ( ), note.size, { plot.x, plot.y + plot.h + 28, plot.w, 20 }, ui::rgb ( THEME_FAINT ), ui::ALIGN_RIGHT );

		}

	}

}