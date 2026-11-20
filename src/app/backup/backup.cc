#include "app/backup/backup.hh"
#include "base/console/console.hh"
#include "base/file/folder.hh"
#include "base/platform/os.hh"
#include "engine/check/check.hh"
#include "strata.h"

namespace app {

	static void erase_side_files ( const char* path ) {

		static const char* const endings [ ] = { "-wal", "-shm" };

		for ( const char* ending : endings ) {

			base::text side;

			side.add ( path ).add ( ending );

			if ( base::path_exists ( side.value ( ) ) )
				base::erase_path ( side.value ( ) );

		}

	}

	bool check_file ( const char* path, base::text& summary, bool& clean ) {

		engine::database* db = base::make < engine::database > ( 1 );
		engine::check_report report = { };

		clean = false;

		if ( !db )
			return false;

		if ( !engine::open_database ( *db, path ) ) {

			summary.add ( "the database could not be opened" );

			if ( db->problem.size )
				summary.add ( ": " ).add ( db->problem.value ( ) );

			db->problem.~text ( );
			base::release ( db );

			return false;

		}

		bool fine = engine::check_database ( *db, report );

		summary.add ( "checked " ).number ( report.pages ).add ( " pages, " ).number ( report.trees ).add ( " trees, " ).number ( report.rows ).add ( " rows, " ).number ( report.entries ).add ( " index entries, " ).number ( report.free_pages ).add ( " free pages\n" );

		for ( uint32_t i = 0; i < report.problem_count; i++ )
			summary.add ( "  problem: " ).add ( report.problems [ i ] ).add ( '\n' );

		clean = fine && !report.problem_count;
		engine::forget_report ( report );
		engine::close_database ( *db );
		db->problem.~text ( );
		base::release ( db );

		return true;

	}

	int32_t run_check ( const char* path ) {

		if ( !base::path_exists ( path ) ) {

			base::write_error ( "there is no database at that path\n" );

			return 2;

		}

		base::text summary;
		bool clean = false;

		if ( !check_file ( path, summary, clean ) ) {

			summary.add ( '\n' );
			base::write_error ( summary.value ( ) );

			return 2;

		}

		summary.add ( clean ? "ok\n" : "the file has problems\n" );
		base::write ( summary.value ( ) );

		return clean ? 0 : 1;

	}

	static bool copy_side ( const char* from, const char* to, const char* ending ) {

		base::text source;
		base::text target;

		source.add ( from ).add ( ending );
		target.add ( to ).add ( ending );

		if ( !base::path_exists ( source.value ( ) ) )
			return true;

		base::text content;

		return base::read_whole ( source.value ( ), content ) && base::save_file ( target.value ( ), content.value ( ), content.size );

	}

	int32_t run_backup ( const char* source, const char* target ) {

		uint64_t started = base::os::ticks ( );

		if ( !base::path_exists ( source ) ) {

			base::write_error ( "there is no database at that path\n" );

			return 2;

		}

		if ( base::path_exists ( target ) ) {

			base::write_error ( "the backup file already exists, choose a new name\n" );

			return 2;

		}

		base::text partial;
		base::text sql;
		strata_db* db = nullptr;

		partial.add ( target ).add ( ".partial" );
		base::erase_path ( partial.value ( ) );
		erase_side_files ( partial.value ( ) );
		sql.add ( "VACUUM INTO '" );

		for ( const char* at = partial.value ( ); *at; at++ ) {

			if ( *at == '\'' )
				sql.add ( '\'' );

			sql.add ( *at );

		}

		sql.add ( '\'' );

		if ( strata_open ( source, &db ) != STRATA_OK ) {

			base::write_error ( "the database could not be opened\n" );

			if ( db )
				strata_close ( db );

			return 2;

		}

		strata_busy_timeout ( db, 60000 );

		if ( strata_exec ( db, sql.value ( ) ) != STRATA_OK ) {

			base::text line;

			line.add ( "the backup failed: " ).add ( strata_error ( db ) ).add ( '\n' );
			base::write_error ( line.value ( ) );
			strata_close ( db );
			base::erase_path ( partial.value ( ) );

			return 1;

		}

		strata_close ( db );

		base::text summary;
		bool clean = false;

		if ( !check_file ( partial.value ( ), summary, clean ) || !clean ) {

			summary.add ( "the backup copy did not pass its check and was removed\n" );
			base::write_error ( summary.value ( ) );
			base::erase_path ( partial.value ( ) );
			erase_side_files ( partial.value ( ) );

			return 1;

		}

		erase_side_files ( partial.value ( ) );

		if ( !copy_side ( source, target, ".users" ) || !copy_side ( source, target, ".grants" ) || !base::replace_path ( partial.value ( ), target ) ) {

			base::write_error ( "the backup could not be put in place\n" );
			base::erase_path ( partial.value ( ) );

			return 1;

		}

		base::file_handle file;
		uint64_t size = 0;

		if ( base::open_reading ( file, target ) ) {

			size = base::file_size ( file );
			base::close_file ( file );

		}

		base::text line;
		uint64_t elapsed = ( base::os::ticks ( ) - started ) * 1000 / base::os::tick_rate ( );

		line.add ( summary.value ( ), summary.size ).add ( "backed up " ).number ( size / 1024 ).add ( " KB to " ).add ( target ).add ( " in " ).number ( elapsed ).add ( " ms\n" );
		base::write ( line.value ( ) );

		return 0;

	}

	int32_t run_restore ( const char* backup, const char* target ) {

		if ( !base::path_exists ( backup ) ) {

			base::write_error ( "there is no backup at that path\n" );

			return 2;

		}

		if ( base::path_exists ( target ) ) {

			base::write_error ( "a database already exists at the target, move it away first so nothing is overwritten\n" );

			return 2;

		}

		base::text summary;
		bool clean = false;

		if ( !check_file ( backup, summary, clean ) || !clean ) {

			summary.add ( "the backup did not pass its check, nothing was restored\n" );
			base::write_error ( summary.value ( ) );

			return 1;

		}

		erase_side_files ( backup );

		base::text partial;

		partial.add ( target ).add ( ".partial" );

		if ( !base::copy_file ( backup, partial.value ( ) ) || !copy_side ( backup, target, ".users" ) || !copy_side ( backup, target, ".grants" ) || !base::replace_path ( partial.value ( ), target ) ) {

			base::write_error ( "the backup could not be restored\n" );
			base::erase_path ( partial.value ( ) );

			return 1;

		}

		summary.add ( "restored " ).add ( backup ).add ( " to " ).add ( target ).add ( '\n' );
		base::write ( summary.value ( ) );

		return 0;

	}

}
