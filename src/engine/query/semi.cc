#include "engine/schema/schema.hh"

namespace engine {

	static bool reaches_out ( const sql::expression* item ) {

		if ( !item )
			return false;

		if ( item->kind == sql::EXPRESSION_COLUMN && item->depth > 0 )
			return true;

		if ( reaches_out ( item->left ) || reaches_out ( item->right ) || reaches_out ( item->third ) )
			return true;

		for ( uint32_t i = 0; item->list && i < item->count; i++ )
			if ( reaches_out ( item->list [ i ] ) )
				return true;

		return false;

	}

	static int32_t affinity_class ( sql::affinity_kind kind ) {

		if ( kind == sql::AFFINITY_INTEGER || kind == sql::AFFINITY_REAL || kind == sql::AFFINITY_NUMERIC )
			return 1;

		return kind == sql::AFFINITY_TEXT ? 2 : 0;

	}

	static bool key_pair ( const sql::expression* term, const sql::expression*& inner, const sql::expression*& outer ) {

		if ( term->kind != sql::EXPRESSION_BINARY || term->op != sql::OPERATOR_EQUAL )
			return false;

		const sql::expression* a = term->left;
		const sql::expression* b = term->right;

		if ( !a || !b || a->kind != sql::EXPRESSION_COLUMN || b->kind != sql::EXPRESSION_COLUMN || a->nocase || b->nocase )
			return false;

		if ( a->depth > 0 && b->depth == 0 ) {

			const sql::expression* held = a;

			a = b;
			b = held;

		}

		if ( a->depth != 0 || a->source != 0 || b->depth == 0 )
			return false;

		int32_t kind = affinity_class ( expression_affinity ( a ) );

		if ( !kind || kind != affinity_class ( expression_affinity ( b ) ) )
			return false;

		inner = a;
		outer = b;

		return true;

	}

	semi_plan* semi_join ( run_context& ctx, prepared_select* child ) {

		if ( child->semi_checked )
			return child->semi;

		child->semi_checked = true;

		if ( child->source_count != 1 || child->grouping || child->aggregate_count || child->window_count || child->having || child->limit || child->offset || child->next || child->constant_count || child->values_only || child->uses_working )
			return nullptr;

		prepared_source& source = child->sources [ 0 ];

		if ( !source.table || source.query || source.cte || source.working || source.left || source.right )
			return nullptr;

		semi_plan* made = ctx.lasting->make < semi_plan > ( 1 );
		sql::expression** rest = ctx.lasting->make < sql::expression* > ( source.filter_count + 1 );
		uint32_t rest_count = 0;

		if ( !made || !rest )
			return nullptr;

		for ( uint32_t t = 0; t < source.filter_count; t++ ) {

			sql::expression* term = source.filters [ t ];
			const sql::expression* inner = nullptr;
			const sql::expression* outer = nullptr;

			if ( uses_query ( term ) )
				return nullptr;

			if ( key_pair ( term, inner, outer ) ) {

				if ( made->key_count == SEMI_KEYS )
					return nullptr;

				made->inner [ made->key_count ] = inner;
				made->outer [ made->key_count ] = outer;
				made->key_count++;

				continue;

			}

			if ( reaches_out ( term ) )
				return nullptr;

			rest [ rest_count++ ] = term;

		}

		if ( !made->key_count )
			return nullptr;

		made->scan = source;
		made->scan.filters = rest;
		made->scan.filter_count = rest_count;
		made->scan.filter_room = rest_count;
		made->scan.path = { };
		made->scan.path.kind = PATH_SCAN;
		made->select = *child;
		made->select.sources = &made->scan;
		made->select.semi = nullptr;
		child->semi = made;

		return made;

	}

	static storage::value plain_key ( const storage::value& item ) {

		if ( item.kind == storage::VALUE_REAL && item.real >= -9.2e18 && item.real <= 9.2e18 && item.real == static_cast < double > ( static_cast < int64_t > ( item.real ) ) )
			return storage::integer_value ( static_cast < int64_t > ( item.real ) );

		return item;

	}

	static bool fill_semi ( run_context& ctx, query_state* scope, semi_plan& semi, value_set& out ) {

		query_state inner = { };
		source_state source = { };

		inner.plan = &semi.select;
		inner.parent = scope;
		inner.sources = &source;
		source.plan = &semi.scan;

		if ( !open_source ( ctx, &inner, source ) )
			return false;

		base::arena_mark mark = ctx.scratch->mark ( );

		for ( ;; ) {

			bool found = false;
			bool fine = true;

			if ( !next_source ( ctx, &inner, source, found ) )
				return false;

			if ( !found )
				return true;

			sql::expression* const* terms = nullptr;
			uint32_t term_count = 0;

			source_filters ( source, terms, term_count );

			if ( !passes ( ctx, &inner, terms, term_count, fine ) )
				return false;

			storage::value keys [ SEMI_KEYS ];

			for ( uint32_t k = 0; fine && k < semi.key_count; k++ ) {

				if ( !evaluate ( ctx, &inner, semi.inner [ k ], keys [ k ] ) )
					return false;

				keys [ k ] = plain_key ( keys [ k ] );
				fine = keys [ k ].kind != storage::VALUE_NULL;

			}

			bool added = false;

			if ( fine && !set_add ( *ctx.results, out, keys, semi.key_count, added ) )
				return fail_run ( ctx, "out of memory" );

			ctx.scratch->rewind ( mark );

		}

	}

	bool semi_exists ( run_context& ctx, query_state* scope, const sql::expression* item, semi_plan& semi, storage::value& out ) {

		cached_child* entry = ctx.cache;

		while ( entry && entry->key != item )
			entry = entry->next;

		if ( !entry ) {

			entry = ctx.results->make < cached_child > ( 1 );

			if ( !entry )
				return fail_run ( ctx, "out of memory" );

			if ( !fill_semi ( ctx, scope, semi, entry->set ) )
				return false;

			entry->key = item;
			entry->next = ctx.cache;
			ctx.cache = entry;

		}

		query_state probe = { };
		storage::value keys [ SEMI_KEYS ];
		bool present = entry->set.count != 0;

		probe.plan = &semi.select;
		probe.parent = scope;

		for ( uint32_t k = 0; present && k < semi.key_count; k++ ) {

			if ( !evaluate ( ctx, &probe, semi.outer [ k ], keys [ k ] ) )
				return false;

			keys [ k ] = plain_key ( keys [ k ] );
			present = keys [ k ].kind != storage::VALUE_NULL;

		}

		if ( present )
			present = set_has ( entry->set, keys, semi.key_count );

		out = storage::integer_value ( present != item->negated ? 1 : 0 );

		return true;

	}

}
