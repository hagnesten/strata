#include "engine/plan/join_order.hh"

namespace engine {

	static bool rowid_name ( const char* name ) {

		return base::same_folded ( name, "rowid" ) || base::same_folded ( name, "oid" ) || base::same_folded ( name, "_rowid_" );

	}

	static double tree_depth ( double rows ) {

		double depth = 1;

		while ( rows > 2 ) {

			rows /= 2;
			depth++;

		}

		return depth;

	}

	static int32_t column_of ( const prepared_source& source, const char* name ) {

		for ( uint32_t c = 0; c < source.width; c++ )
			if ( base::same_folded ( source.names [ c ], name ) )
				return static_cast < int32_t > ( c );

		return rowid_name ( name ) ? -1 : -2;

	}

	static int32_t resolve ( const prepared_select* plan, const sql::expression* item, int32_t& column ) {

		int32_t found = -1;

		for ( uint32_t s = 0; s < plan->source_count; s++ ) {

			const prepared_source& source = plan->sources [ s ];

			if ( item->table && !base::same_folded ( item->table, source.alias ) )
				continue;

			int32_t at = column_of ( source, item->name );

			if ( at == -2 || ( at == -1 && !item->table && plan->source_count > 1 ) )
				continue;

			if ( found >= 0 )
				return -2;

			found = static_cast < int32_t > ( s );
			column = at;

		}

		return found;

	}

	static uint64_t needs_of ( const prepared_select* plan, const sql::expression* item ) {

		if ( !item )
			return 0;

		uint64_t needs = 0;

		if ( item->kind == sql::EXPRESSION_COLUMN && item->name ) {

			int32_t column = 0;
			int32_t source = resolve ( plan, item, column );

			if ( source >= 0 )
				needs |= 1ull << source;
			else if ( source == -2 )
				needs = ~0ull;

		}

		needs |= needs_of ( plan, item->left ) | needs_of ( plan, item->right ) | needs_of ( plan, item->third );

		for ( uint32_t i = 0; i < item->count; i++ )
			needs |= needs_of ( plan, item->list [ i ] );

		return needs;

	}

	static void add_side ( join_graph& graph, const sql::expression* column, const sql::expression* other ) {

		if ( !column || column->kind != sql::EXPRESSION_COLUMN || !column->name || graph.count == JOIN_TERM_ROOM )
			return;

		int32_t at = 0;
		int32_t source = resolve ( graph.plan, column, at );

		if ( source < 0 )
			return;

		uint64_t needs = needs_of ( graph.plan, other );

		if ( needs & 1ull << source )
			return;

		graph.terms [ graph.count++ ] = { static_cast < uint32_t > ( source ), at, needs };

	}

	static void collect_terms ( join_graph& graph, const sql::expression* item ) {

		if ( !item )
			return;

		if ( item->kind == sql::EXPRESSION_BINARY && item->op == sql::OPERATOR_AND ) {

			collect_terms ( graph, item->left );
			collect_terms ( graph, item->right );

			return;

		}

		if ( item->kind == sql::EXPRESSION_BINARY && ( item->op == sql::OPERATOR_EQUAL || item->op == sql::OPERATOR_IS ) ) {

			add_side ( graph, item->left, item->right );
			add_side ( graph, item->right, item->left );

		}

	}

	static bool has_term ( const join_graph& graph, uint32_t source, int32_t column, uint64_t placed ) {

		for ( uint32_t t = 0; t < graph.count; t++ ) {

			const join_term& term = graph.terms [ t ];

			if ( term.target == source && term.column == column && !( term.needs & ~placed ) )
				return true;

		}

		return false;

	}

	static join_step place ( run_context& ctx, const join_graph& graph, uint32_t source, uint64_t placed ) {

		const table_info& table = *graph.plan->sources [ source ].table;
		double rows = graph.rows [ source ];
		double probe_rows = rows;
		double probe_cost = rows;
		uint32_t usable = 0;
		uint32_t spent = 0;

		for ( uint32_t t = 0; t < graph.count; t++ )
			if ( graph.terms [ t ].target == source && !( graph.terms [ t ].needs & ~placed ) )
				usable++;

		if ( has_term ( graph, source, -1, placed ) || ( table.rowid_column >= 0 && has_term ( graph, source, table.rowid_column, placed ) ) ) {

			probe_rows = 1;
			probe_cost = tree_depth ( rows );
			spent = 1;

		} else {

			for ( uint32_t i = 0; i < ctx.schema->index_count; i++ ) {

				const index_info& index = ctx.schema->indexes [ i ];

				if ( index.fulltext || !base::same_folded ( index.table, table.name ) )
					continue;

				uint32_t k = 0;

				while ( k < index.column_count && has_term ( graph, source, static_cast < int32_t > ( index.columns [ k ] ), placed ) )
					k++;

				if ( !k )
					continue;

				double expected = 0;

				if ( index.unique && k == index.column_count )
					expected = 1;
				else if ( index.stat_known )
					expected = static_cast < double > ( index.stat_average [ k - 1 ] );
				else
					expected = base::min < double > ( rows, base::max < double > ( 1, 10.0 / static_cast < double > ( k ) ) );

				double cost = tree_depth ( rows ) + expected;

				if ( cost < probe_cost ) {

					probe_cost = cost;
					probe_rows = expected;
					spent = k;

				}

			}

		}

		double out = probe_rows;

		for ( uint32_t extra = spent; extra < usable; extra++ )
			out *= 0.25;

		return { probe_cost, base::max < double > ( 1, out ) };

	}

	static double order_cost ( run_context& ctx, const join_graph& graph, const uint32_t* order ) {

		double cost = 0;
		double outer = 1;
		uint64_t placed = 0;

		for ( uint32_t p = 0; p < graph.source_count; p++ ) {

			join_step step = place ( ctx, graph, order [ p ], placed );

			cost += outer * step.probe_cost;
			outer *= step.out_rows;
			placed |= 1ull << order [ p ];

		}

		return cost;

	}

	static void search ( run_context& ctx, join_search& state, uint32_t depth, uint64_t placed, double cost, double outer ) {

		if ( cost >= state.best_cost )
			return;

		const join_graph& graph = *state.graph;

		if ( depth == graph.source_count ) {

			state.best_cost = cost;
			base::copy ( state.best, state.order, sizeof ( uint32_t ) * graph.source_count );

			return;

		}

		for ( uint32_t s = 0; s < graph.source_count; s++ ) {

			if ( state.used [ s ] )
				continue;

			join_step step = place ( ctx, graph, s, placed );

			state.used [ s ] = true;
			state.order [ depth ] = s;
			search ( ctx, state, depth + 1, placed | 1ull << s, cost + outer * step.probe_cost, outer * step.out_rows );
			state.used [ s ] = false;

		}

	}

	static void greedy ( run_context& ctx, join_search& state ) {

		const join_graph& graph = *state.graph;
		uint64_t placed = 0;
		double outer = 1;

		for ( uint32_t depth = 0; depth < graph.source_count; depth++ ) {

			uint32_t pick = 0;
			double pick_cost = 0;
			join_step pick_step = { };
			bool any = false;

			for ( uint32_t s = 0; s < graph.source_count; s++ ) {

				if ( state.used [ s ] )
					continue;

				join_step step = place ( ctx, graph, s, placed );
				double cost = outer * step.probe_cost;

				if ( !any || cost < pick_cost ) {

					any = true;
					pick = s;
					pick_cost = cost;
					pick_step = step;

				}

			}

			state.used [ pick ] = true;
			state.best [ depth ] = pick;
			placed |= 1ull << pick;
			outer *= pick_step.out_rows;

		}

	}

	static bool can_reorder ( const prepared_select* plan ) {

		const sql::select_statement* syntax = plan->syntax;

		if ( plan->source_count < 2 || plan->source_count > JOIN_SOURCE_ROOM || syntax->fixed_order )
			return false;

		for ( uint32_t s = 0; s < plan->source_count; s++ ) {

			const prepared_source& source = plan->sources [ s ];
			const sql::table_source& from = *source.syntax;

			if ( !source.table || source.query || source.cte || from.natural || from.using_count || from.join == sql::JOIN_LEFT || from.join == sql::JOIN_RIGHT || from.join == sql::JOIN_FULL )
				return false;

		}

		return true;

	}

	void order_joins ( run_context& ctx, prepared_select* plan ) {

		if ( !can_reorder ( plan ) )
			return;

		join_graph* graph = ctx.scratch->make < join_graph > ( 1 );
		join_search* state = ctx.scratch->make < join_search > ( 1 );

		if ( !graph || !state )
			return;

		graph->plan = plan;
		graph->source_count = plan->source_count;
		collect_terms ( *graph, plan->syntax->where );

		for ( uint32_t s = 0; s < plan->source_count; s++ ) {

			collect_terms ( *graph, plan->sources [ s ].syntax->on );
			graph->rows [ s ] = base::max < double > ( 1, static_cast < double > ( table_rows ( *ctx.pages, *plan->sources [ s ].table ) ) );
			state->order [ s ] = s;
			state->best [ s ] = s;

		}

		state->graph = graph;

		if ( plan->source_count <= JOIN_SEARCH_LIMIT ) {

			state->best_cost = order_cost ( ctx, *graph, state->order ) * 0.9;
			search ( ctx, *state, 0, 0, 0, 1 );

		} else
			greedy ( ctx, *state );

		bool moved = false;

		for ( uint32_t s = 0; s < plan->source_count; s++ )
			moved = moved || state->best [ s ] != s;

		if ( !moved )
			return;

		prepared_source* ordered = ctx.lasting->make < prepared_source > ( plan->source_count );

		if ( !ordered )
			return;

		for ( uint32_t s = 0; s < plan->source_count; s++ )
			ordered [ s ] = plan->sources [ state->best [ s ] ];

		plan->sources = ordered;

	}

}