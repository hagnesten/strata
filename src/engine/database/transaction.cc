#include "engine/database/database.hh"

namespace engine {

	void forget_savepoints ( database& item ) {

		for ( uint32_t i = 0; i < item.savepoints.count; i++ )
			base::release ( item.savepoints.items [ i ].name );

		base::release ( item.savepoints.items );
		item.savepoints = { };

	}

	static void drop_savepoints ( database& item, uint32_t keep ) {

		while ( item.savepoints.count > keep )
			base::release ( item.savepoints.items [ --item.savepoints.count ].name );

	}

	static bool find_savepoint ( database& item, run_context& ctx, const char* name, uint32_t& out ) {

		for ( uint32_t i = item.savepoints.count; i-- > 0; ) {

			if ( base::same_folded ( item.savepoints.items [ i ].name, name ) ) {

				out = i;

				return true;

			}

		}

		base::text line;

		line.add ( "no such savepoint: " ).add ( name );

		return fail_run ( ctx, line.value ( ) );

	}

	static bool push_savepoint ( database& item, run_context& ctx, const char* name, bool opened ) {

		savepoint_stack& stack = item.savepoints;

		if ( stack.count == stack.room ) {

			uint32_t grown = stack.room ? stack.room * 2 : 8;
			savepoint* made = base::make < savepoint > ( grown );

			if ( !made )
				return fail_run ( ctx, "out of memory" );

			if ( stack.items ) {

				base::copy ( made, stack.items, sizeof ( savepoint ) * stack.count );
				base::release ( stack.items );

			}

			stack.items = made;
			stack.room = grown;

		}

		size_t size = base::length ( name );
		char* copy = base::make < char > ( size + 1 );

		if ( !copy )
			return fail_run ( ctx, "out of memory" );

		base::copy_text ( copy, size + 1, name );

		if ( !storage::push_mark ( item.pages ) ) {

			base::release ( copy );

			return pager_problem ( item, "the savepoint could not be made" );

		}

		stack.items [ stack.count++ ] = { copy, opened };

		return true;

	}

	static bool commit_open ( database& item ) {

		item.in_transaction = false;
		forget_savepoints ( item );

		if ( storage::commit_pager ( item.pages ) ) {

			storage::end_read ( item.pages );

			return true;

		}

		pager_problem ( item, "the transaction could not be committed and was rolled back" );
		storage::rollback_pager ( item.pages );
		storage::end_read ( item.pages );
		reload_schema ( item );

		return false;

	}

	bool control_statement ( database& item, run_context& ctx, const sql::statement* statement, bool& handled ) {

		handled = true;

		switch ( statement->kind ) {

			case sql::STATEMENT_BEGIN:
				if ( item.in_transaction )
					return fail_run ( ctx, "a transaction is already open" );

				if ( !storage::begin_read ( item.pages ) || ( statement->immediate && !storage::begin_write ( item.pages ) ) ) {

					storage::end_read ( item.pages );

					return pager_problem ( item, "the transaction could not start" );

				}

				item.in_transaction = true;

				return true;

			case sql::STATEMENT_COMMIT:
				if ( !item.in_transaction )
					return fail_run ( ctx, "there is no open transaction to commit" );

				return commit_open ( item );

			case sql::STATEMENT_ROLLBACK: {

				if ( !statement->savepoint ) {

					if ( !item.in_transaction )
						return fail_run ( ctx, "there is no open transaction to roll back" );

					item.in_transaction = false;
					forget_savepoints ( item );
					storage::rollback_pager ( item.pages );
					storage::end_read ( item.pages );

					return item.schema->version == item.pages.header.schema_version || reload_schema ( item );

				}

				uint32_t at = 0;

				if ( !find_savepoint ( item, ctx, statement->savepoint, at ) )
					return false;

				storage::undo_to_mark ( item.pages, at, true );
				drop_savepoints ( item, at + 1 );

				return item.schema->version == item.pages.header.schema_version || reload_schema ( item );

			}

			case sql::STATEMENT_SAVEPOINT: {

				bool opened = !item.in_transaction;

				if ( opened && !storage::begin_read ( item.pages ) )
					return pager_problem ( item, "the transaction could not start" );

				if ( !push_savepoint ( item, ctx, statement->savepoint, opened ) )
					return false;

				item.in_transaction = true;

				return true;

			}

			case sql::STATEMENT_RELEASE: {

				uint32_t at = 0;

				if ( !find_savepoint ( item, ctx, statement->savepoint, at ) )
					return false;

				if ( at == 0 && item.savepoints.items [ 0 ].opened )
					return commit_open ( item );

				if ( !storage::release_mark ( item.pages, at ) )
					return pager_problem ( item, "the savepoint could not be released" );

				drop_savepoints ( item, at );

				return true;

			}

			default:
				handled = false;

				return true;

		}

	}

}