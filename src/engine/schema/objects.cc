#include "engine/schema/schema.hh"
#include "sql/print/print.hh"

#define SEQUENCE_PREFIX "strata_seq:"

namespace engine {

	static const char* qualified_name ( run_context& ctx, const char* name ) {

		char qualified [ CATALOG_NAME_ROOM ];

		if ( !search_name ( *ctx.schema, name, qualified, sizeof ( qualified ) ) )
			return name;

		return ctx.lasting->keep ( qualified, base::length ( qualified ) );

	}

	bool run_create_view ( run_context& ctx, const sql::statement* item ) {

		const sql::create_view_statement* view = item->create_view;
		const char* name = qualified_name ( ctx, view->name );

		if ( !name )
			return fail_run ( ctx, "out of memory" );

		view_info* old = find_view ( *ctx.schema, name );
		int64_t rowid = 0;

		if ( old ) {

			if ( view->if_not_exists )
				return true;

			if ( !view->replace )
				return fail_named ( ctx, "the name ", name, " is already used by a view" );

			rowid = old->schema_rowid;

		} else {

			bool skip = false;

			if ( !name_taken ( ctx, name, view->if_not_exists, skip ) )
				return false;

			if ( skip )
				return true;

		}

		if ( reserved_name ( name ) )
			return fail_run ( ctx, "names starting with strata_ are kept for Strata itself" );

		prepared_select* plan = prepare_select ( ctx, view->query, nullptr );

		if ( !plan )
			return false;

		if ( view->column_count > plan->output_count )
			return fail_named ( ctx, "the view ", name, " names more columns than its query returns" );

		for ( uint32_t a = 0; a < view->column_count; a++ )
			for ( uint32_t b = 0; b < a; b++ )
				if ( base::same_folded ( view->columns [ a ], view->columns [ b ] ) )
					return fail_named ( ctx, "the view ", name, " names a column twice" );

		char* text = ctx.lasting->keep ( item->text, item->text_size );

		if ( !text )
			return fail_run ( ctx, "out of memory" );

		if ( !write_schema_row ( *ctx.pages, "view", name, name, 0, text, rowid ) )
			return fail_storage ( ctx, "the schema could not be written" );

		return refresh_schema ( ctx );

	}

	bool run_drop_view ( run_context& ctx, const sql::drop_statement* item ) {

		view_info* view = find_view ( *ctx.schema, item->name );

		if ( !view ) {

			if ( item->if_exists )
				return true;

			if ( find_table ( *ctx.schema, item->name ) )
				return fail_named ( ctx, "", item->name, " is a table, not a view" );

			return fail_named ( ctx, "no such view: ", item->name, "" );

		}

		if ( !delete_schema_row ( *ctx.pages, view->schema_rowid ) )
			return fail_storage ( ctx, "the view could not be dropped" );

		return refresh_schema ( ctx );

	}

	sql::select_statement* user_view ( run_context& ctx, const char* name, const char**& columns, uint32_t& column_count ) {

		columns = nullptr;
		column_count = 0;

		view_info* view = find_view ( *ctx.schema, name );

		if ( !view )
			return nullptr;

		char* text = ctx.lasting->keep ( view->sql, base::length ( view->sql ) );
		sql::statement* parsed = nullptr;
		sql::parse_error error = { };
		size_t consumed = 0;

		if ( !text ) {

			fail_run ( ctx, "out of memory" );

			return nullptr;

		}

		if ( !sql::parse_one ( *ctx.lasting, text, consumed, parsed, error ) || !parsed || parsed->kind != sql::STATEMENT_CREATE_VIEW ) {

			fail_named ( ctx, "the stored view ", view->name, " could not be read" );

			return nullptr;

		}

		columns = parsed->create_view->columns;
		column_count = parsed->create_view->column_count;

		return parsed->create_view->query;

	}

	static void sequence_text ( base::text& out, const char* name, const sequence_info& info ) {

		out.clear ( );
		out.add ( "CREATE SEQUENCE " );
		sql::print_name ( out, name );
		out.add ( " INCREMENT BY " ).signed_number ( info.increment );
		out.add ( " MINVALUE " ).signed_number ( info.minimum );
		out.add ( " MAXVALUE " ).signed_number ( info.maximum );
		out.add ( " START WITH " ).signed_number ( info.start );
		out.add ( info.cycle ? " CYCLE" : " NO CYCLE" );

	}

	static bool sound_limits ( run_context& ctx, const sequence_info& info ) {

		if ( info.minimum >= info.maximum )
			return fail_named ( ctx, "MINVALUE must be less than MAXVALUE for sequence ", info.name, "" );

		if ( info.start < info.minimum || info.start > info.maximum )
			return fail_named ( ctx, "START value of sequence ", info.name, " is outside MINVALUE and MAXVALUE" );

		return true;

	}

	static void counter_name ( base::text& out, const char* name ) {

		out.clear ( );
		out.add ( SEQUENCE_PREFIX ).add ( name );

	}

	static bool place_counter ( run_context& ctx, const char* name, int64_t next, int64_t increment ) {

		base::text key;
		int64_t last = 0;

		counter_name ( key, name );

		if ( __builtin_sub_overflow ( next, increment, &last ) )
			last = next;

		return write_counter ( ctx, key.value ( ), last );

	}

	bool run_create_sequence ( run_context& ctx, const sql::statement* item ) {

		const sql::sequence_statement* syntax = item->sequence;
		const char* name = qualified_name ( ctx, syntax->name );
		bool skip = false;

		if ( !name )
			return fail_run ( ctx, "out of memory" );

		if ( !name_taken ( ctx, name, syntax->if_not_exists, skip ) )
			return false;

		if ( skip )
			return true;

		if ( reserved_name ( name ) )
			return fail_run ( ctx, "names starting with strata_ are kept for Strata itself" );

		sequence_info info = { };

		info.name = name;
		sequence_limits ( *syntax, info );

		if ( !sound_limits ( ctx, info ) || !ensure_sequence_table ( ctx ) )
			return false;

		base::text text;
		int64_t rowid = 0;

		sequence_text ( text, name, info );

		if ( !write_schema_row ( *ctx.pages, "sequence", name, name, 0, text.value ( ), rowid ) )
			return fail_storage ( ctx, "the schema could not be written" );

		if ( !refresh_schema ( ctx ) )
			return false;

		base::text key;

		counter_name ( key, name );

		return remove_counter ( ctx, key.value ( ) );

	}

	bool run_alter_sequence ( run_context& ctx, const sql::sequence_statement* item ) {

		sequence_info* found = find_sequence ( *ctx.schema, item->name );

		if ( !found )
			return item->if_not_exists ? true : fail_named ( ctx, "no such sequence: ", item->name, "" );

		sequence_info info = *found;
		const char* name = ctx.lasting->keep ( found->name, base::length ( found->name ) );
		int64_t rowid = found->schema_rowid;

		if ( !name )
			return fail_run ( ctx, "out of memory" );

		info.name = name;

		if ( item->has_increment )
			info.increment = item->increment;

		if ( item->has_minimum )
			info.minimum = item->minimum;

		if ( item->has_maximum )
			info.maximum = item->maximum;

		if ( item->has_start )
			info.start = item->start;

		if ( item->has_cycle )
			info.cycle = item->cycle;

		if ( !sound_limits ( ctx, info ) )
			return false;

		base::text text;

		sequence_text ( text, name, info );

		if ( !write_schema_row ( *ctx.pages, "sequence", name, name, 0, text.value ( ), rowid ) )
			return fail_storage ( ctx, "the schema could not be written" );

		if ( !refresh_schema ( ctx ) )
			return false;

		if ( !item->has_restart )
			return true;

		int64_t next = item->restart_given ? item->restart : info.start;

		if ( next < info.minimum || next > info.maximum )
			return fail_named ( ctx, "RESTART value of sequence ", name, " is outside MINVALUE and MAXVALUE" );

		return place_counter ( ctx, name, next, info.increment );

	}

	bool run_drop_sequence ( run_context& ctx, const sql::drop_statement* item ) {

		sequence_info* found = find_sequence ( *ctx.schema, item->name );

		if ( !found )
			return item->if_exists ? true : fail_named ( ctx, "no such sequence: ", item->name, "" );

		base::text key;

		counter_name ( key, found->name );

		if ( !remove_counter ( ctx, key.value ( ) ) || !delete_schema_row ( *ctx.pages, found->schema_rowid ) )
			return fail_storage ( ctx, "the sequence could not be dropped" );

		return refresh_schema ( ctx );

	}

	static void bare_name ( const char* name, base::text& out ) {

		out.clear ( );

		const char* at = name;

		if ( base::starts_with ( at, "public." ) || base::starts_with ( at, "PUBLIC." ) )
			at += 7;
		else if ( base::starts_with ( at, "\"public\"." ) )
			at += 9;

		for ( ; *at; at++ )
			if ( *at != '"' )
				out.add ( *at );

	}

	static table_info* identity_owner ( run_context& ctx, const char* name ) {

		base::text candidate;

		for ( uint32_t t = 0; t < ctx.schema->table_count; t++ ) {

			table_info& table = ctx.schema->tables [ t ];

			if ( table.rowid_column < 0 || reserved_name ( table.name ) )
				continue;

			candidate.clear ( );
			candidate.add ( catalog_view_alias ( table.name ) ).add ( '_' ).add ( table.columns [ table.rowid_column ].name ).add ( "_seq" );

			if ( base::same_folded ( candidate.value ( ), name ) )
				return &table;

		}

		return nullptr;

	}

	bool sequence_step ( run_context& ctx, const char* name, int32_t action, int64_t value, bool called, int64_t& out ) {

		base::text bare;

		bare_name ( name, bare );

		sequence_info* sequence = find_sequence ( *ctx.schema, bare.value ( ) );

		if ( !sequence ) {

			table_info* table = identity_owner ( ctx, bare.value ( ) );

			if ( !table ) {

				base::text message;

				message.add ( "relation \"" ).add ( bare.value ( ) ).add ( "\" does not exist" );

				return fail_run ( ctx, message.value ( ) );

			}

			storage::btree tree = { ctx.pages, table->root, false };
			int64_t last = 0;
			int64_t high = 0;
			bool empty = true;
			bool found = false;

			if ( !storage::table_last ( tree, last, empty ) )
				return fail_storage ( ctx, "a table could not be read" );

			if ( empty )
				last = 0;

			if ( !read_counter ( ctx, table->name, high, found ) )
				return false;

			int64_t top = base::max ( last, high );

			if ( action == 1 ) {

				out = top;

				return true;

			}

			if ( action == 2 ) {

				out = value;

				return write_counter ( ctx, table->name, called ? value : value - 1 );

			}

			if ( top == 0x7FFFFFFFFFFFFFFFll )
				return fail_named ( ctx, "nextval: reached maximum value of sequence ", bare.value ( ), "" );

			out = top + 1;

			return write_counter ( ctx, table->name, out );

		}

		base::text key;
		int64_t last = 0;
		bool found = false;

		counter_name ( key, sequence->name );

		if ( !read_counter ( ctx, key.value ( ), last, found ) )
			return false;

		if ( action == 1 ) {

			if ( !found ) {

				base::text message;

				message.add ( "currval of sequence \"" ).add ( bare.value ( ) ).add ( "\" is not yet defined in this session" );

				return fail_run ( ctx, message.value ( ) );

			}

			out = last;

			return true;

		}

		if ( action == 2 ) {

			if ( value < sequence->minimum || value > sequence->maximum ) {

				base::text message;

				message.add ( "setval: value " ).signed_number ( value ).add ( " is out of bounds for sequence \"" ).add ( bare.value ( ) ).add ( '"' );

				return fail_run ( ctx, message.value ( ) );

			}

			out = value;

			return called ? write_counter ( ctx, key.value ( ), value ) : place_counter ( ctx, sequence->name, value, sequence->increment );

		}

		int64_t next = sequence->start;
		bool beyond = false;

		if ( found )
			beyond = __builtin_add_overflow ( last, sequence->increment, &next );

		beyond = beyond || next > sequence->maximum || next < sequence->minimum;

		if ( beyond ) {

			if ( !sequence->cycle ) {

				base::text message;

				message.add ( "nextval: reached " ).add ( sequence->increment > 0 ? "maximum" : "minimum" ).add ( " value of sequence \"" ).add ( bare.value ( ) ).add ( "\" (" ).signed_number ( sequence->increment > 0 ? sequence->maximum : sequence->minimum ).add ( ')' );

				return fail_run ( ctx, message.value ( ) );

			}

			next = sequence->increment > 0 ? sequence->minimum : sequence->maximum;

		}

		out = next;

		return write_counter ( ctx, key.value ( ), next );

	}

}
