#include "engine/run/run.hh"

namespace engine {

	static bool explain_row ( run_context& ctx, row_sink& sink, uint32_t id, uint32_t parent, const base::text& detail ) {

		storage::value values [ 3 ];
		char* kept = ctx.scratch->keep ( detail.value ( ), detail.size );

		if ( !kept )
			return fail_run ( ctx, "out of memory" );

		values [ 0 ] = storage::integer_value ( id );
		values [ 1 ] = storage::integer_value ( parent );
		values [ 2 ] = storage::text_value ( kept, detail.size );

		return sink.emit ( sink.context, values, 3 ) || !ctx.problem->size;

	}

	static bool explain_select ( run_context& ctx, prepared_select* plan, uint32_t parent, row_sink& sink, uint32_t& next );

	static bool explain_core ( run_context& ctx, prepared_select* core, uint32_t parent, row_sink& sink, uint32_t& next ) {

		for ( plan_child* child = core->children; child; child = child->next ) {

			base::text detail;
			uint32_t id = ++next;

			switch ( child->role ) {

				case CHILD_SCALAR: detail.add ( child->plan->correlated ? "CORRELATED SCALAR SUBQUERY " : "SCALAR SUBQUERY " ); break;
				case CHILD_LIST: detail.add ( child->plan->correlated ? "CORRELATED LIST SUBQUERY " : "LIST SUBQUERY " ); break;
				case CHILD_EXISTS: detail.add ( child->plan->correlated ? "CORRELATED EXISTS SUBQUERY " : "EXISTS SUBQUERY " ); break;
				case CHILD_FROM: detail.add ( "MATERIALIZE " ); break;

			}

			detail.number ( child->plan->id );

			if ( !explain_row ( ctx, sink, id, parent, detail ) || !explain_select ( ctx, child->plan, id, sink, next ) )
				return false;

		}

		if ( core->values_only ) {

			base::text detail;

			detail.add ( "SCAN " ).number ( core->syntax->value_rows ).add ( core->syntax->value_rows == 1 ? " CONSTANT ROW" : " CONSTANT ROWS" );

			return explain_row ( ctx, sink, ++next, parent, detail );

		}

		if ( !core->source_count ) {

			base::text detail;

			detail.add ( "SCAN CONSTANT ROW" );

			if ( !explain_row ( ctx, sink, ++next, parent, detail ) )
				return false;

		}

		for ( uint32_t i = 0; i < core->source_count; i++ ) {

			base::text detail;

			describe_path ( core->sources [ i ], detail );

			if ( core->sources [ i ].left && core->sources [ i ].right )
				detail.add ( " (FULL JOIN)" );
			else if ( core->sources [ i ].right )
				detail.add ( " (RIGHT JOIN)" );
			else if ( core->sources [ i ].left )
				detail.add ( " (LEFT JOIN)" );

			if ( !explain_row ( ctx, sink, ++next, parent, detail ) )
				return false;

		}

		if ( core->group_count ) {

			base::text detail;

			detail.add ( "USE HASH TABLE FOR GROUP BY" );

			if ( !explain_row ( ctx, sink, ++next, parent, detail ) )
				return false;

		}

		if ( core->distinct ) {

			base::text detail;

			detail.add ( "USE HASH TABLE FOR DISTINCT" );

			if ( !explain_row ( ctx, sink, ++next, parent, detail ) )
				return false;

		}

		return true;

	}

	static bool explain_select ( run_context& ctx, prepared_select* plan, uint32_t parent, row_sink& sink, uint32_t& next ) {

		if ( !plan->next ) {

			if ( !explain_core ( ctx, plan, parent, sink, next ) )
				return false;

		} else {

			base::text detail;
			uint32_t compound = ++next;

			detail.add ( "COMPOUND QUERY" );

			if ( !explain_row ( ctx, sink, compound, parent, detail ) )
				return false;

			for ( prepared_select* core = plan; core; core = core->next ) {

				base::text label;
				uint32_t id = ++next;

				if ( core == plan )
					label.add ( "LEFT-MOST SUBQUERY" );
				else {

					sql::compound_kind kind = sql::COMPOUND_NONE;

					for ( prepared_select* before = plan; before; before = before->next )
						if ( before->next == core )
							kind = before->compound;

					label.add ( kind == sql::COMPOUND_UNION_ALL ? "UNION ALL" : kind == sql::COMPOUND_UNION ? "UNION USING HASH TABLE" : kind == sql::COMPOUND_INTERSECT ? "INTERSECT USING HASH TABLE" : "EXCEPT USING HASH TABLE" );

				}

				if ( !explain_row ( ctx, sink, id, compound, label ) || !explain_core ( ctx, core, id, sink, next ) )
					return false;

			}

		}

		if ( plan->order_count && !plan->order_by_index ) {

			base::text detail;

			detail.add ( "USE TEMP SORT FOR ORDER BY" );

			if ( !explain_row ( ctx, sink, ++next, parent, detail ) )
				return false;

		}

		return true;

	}

	bool explain_plan ( run_context& ctx, prepared_select* plan, row_sink& sink ) {

		uint32_t next = 0;

		return explain_select ( ctx, plan, 0, sink, next );

	}

}
