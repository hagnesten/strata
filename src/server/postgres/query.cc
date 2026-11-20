#include "server/server.hh"

namespace server {

	static void drop_statement ( server_connection& link, const char* name );

	static bool word_at ( const char* text, size_t size, size_t& at, const char* word ) {

		size_t start = at;

		while ( start < size && ( text [ start ] == ' ' || text [ start ] == '\t' || text [ start ] == '\r' || text [ start ] == '\n' ) )
			start++;

		size_t length = base::length ( word );

		if ( start + length > size )
			return false;

		for ( size_t i = 0; i < length; i++ )
			if ( base::lower ( text [ start + i ] ) != word [ i ] )
				return false;

		if ( start + length < size && ( ( text [ start + length ] >= 'a' && text [ start + length ] <= 'z' ) || ( text [ start + length ] >= 'A' && text [ start + length ] <= 'Z' ) || text [ start + length ] == '_' ) )
			return false;

		at = start + length;

		return true;

	}

	static void search_path ( const char* sql, size_t size, size_t at, char* out, size_t room ) {

		if ( !word_at ( sql, size, at, "to" ) ) {

			while ( at < size && sql [ at ] == ' ' )
				at++;

			if ( at < size && sql [ at ] == '=' )
				at++;

		}

		for ( ;; ) {

			while ( at < size && ( sql [ at ] == ' ' || sql [ at ] == ',' || sql [ at ] == '\t' ) )
				at++;

			if ( at >= size || sql [ at ] == ';' )
				break;

			char quote = sql [ at ] == '"' || sql [ at ] == '\'' ? sql [ at++ ] : 0;
			size_t length = 0;

			while ( at < size && ( quote ? sql [ at ] != quote : sql [ at ] != ',' && sql [ at ] != ' ' && sql [ at ] != ';' ) ) {

				if ( length + 1 < room )
					out [ length++ ] = quote ? sql [ at ] : base::lower ( sql [ at ] );

				at++;

			}

			if ( quote && at < size )
				at++;

			out [ length ] = 0;

			if ( length && out [ 0 ] != '$' )
				return;

		}

		out [ 0 ] = 0;

	}

	bool control_statement ( const char* sql, size_t size, char* tag, size_t tag_room, char* show, size_t show_room, bool& handled ) {

		size_t at = 0;

		handled = true;
		show [ 0 ] = 0;

		if ( word_at ( sql, size, at, "set" ) ) {

			base::copy_text ( tag, tag_room, "SET" );

			if ( !word_at ( sql, size, at, "local" ) )
				word_at ( sql, size, at, "session" );

			if ( word_at ( sql, size, at, "search_path" ) )
				search_path ( sql, size, at, show, show_room );

			return true;

		}

		if ( word_at ( sql, size, at, "reset" ) ) {

			base::copy_text ( tag, tag_room, "RESET" );

			return true;

		}

		at = 0;

		if ( word_at ( sql, size, at, "unlisten" ) ) {

			base::copy_text ( tag, tag_room, "UNLISTEN" );

			return true;

		}

		if ( word_at ( sql, size, at, "discard" ) ) {

			base::copy_text ( tag, tag_room, "DISCARD ALL" );

			return true;

		}

		static const char* const quiet [ ] [ 2 ] = {

			{ "close", "CLOSE CURSOR ALL" },
			{ "lock", "LOCK TABLE" },
			{ "checkpoint", "CHECKPOINT" },
			{ "listen", "LISTEN" },
			{ "notify", "NOTIFY" },
			{ "comment", "COMMENT" },
			{ "grant", "GRANT" },
			{ "revoke", "REVOKE" },

		};

		for ( const auto& pair : quiet ) {

			at = 0;

			if ( word_at ( sql, size, at, pair [ 0 ] ) ) {

				base::copy_text ( tag, tag_room, pair [ 1 ] );

				return true;

			}

		}

		at = 0;

		if ( word_at ( sql, size, at, "deallocate" ) ) {

			base::copy_text ( tag, tag_room, "DEALLOCATE" );
			word_at ( sql, size, at, "prepare" );

			while ( at < size && sql [ at ] == ' ' )
				at++;

			size_t length = 0;
			char quote = at < size && sql [ at ] == '"' ? sql [ at++ ] : 0;

			while ( at < size && length + 1 < show_room && sql [ at ] != ';' && ( quote ? sql [ at ] != quote : sql [ at ] != ' ' ) )
				show [ length++ ] = sql [ at++ ];

			show [ length ] = 0;

			return true;

		}

		at = 0;

		if ( word_at ( sql, size, at, "show" ) ) {

			char name [ SERVER_NAME_ROOM ];
			size_t length = 0;

			while ( at < size && ( sql [ at ] == ' ' || sql [ at ] == '\t' ) )
				at++;

			while ( at < size && length + 1 < sizeof ( name ) && sql [ at ] != ';' && sql [ at ] != ' ' && sql [ at ] != '\r' && sql [ at ] != '\n' )
				name [ length++ ] = sql [ at++ ];

			name [ length ] = 0;
			base::copy_text ( show, show_room, name );
			base::copy_text ( tag, tag_room, name );

			return true;

		}

		at = 0;

		if ( word_at ( sql, size, at, "start" ) || word_at ( sql, size, at, "begin" ) ) {

			base::copy_text ( tag, tag_room, "BEGIN" );

			return true;

		}

		at = 0;

		if ( word_at ( sql, size, at, "end" ) ) {

			base::copy_text ( tag, tag_room, "COMMIT" );

			return true;

		}

		at = 0;

		if ( word_at ( sql, size, at, "abort" ) ) {

			base::copy_text ( tag, tag_room, "ROLLBACK" );

			return true;

		}

		handled = false;

		return true;

	}

	static uint32_t type_size ( uint32_t type ) {

		switch ( type ) {

			case PG_BOOL: return 1;
			case PG_INT2: return 2;
			case PG_INT4: case PG_FLOAT4: case PG_DATE: case PG_OID: return 4;
			case PG_INT8: case PG_FLOAT8: case PG_TIMESTAMP: case PG_TIMESTAMPTZ: case PG_TIME: return 8;
			case PG_UUID: return 16;
			default: return 0xFFFF;

		}

	}

	static void describe_rows ( server_connection& link, server_statement& statement, const int16_t* formats, uint32_t format_count ) {

		begin_message ( link, 'T' );
		put16 ( link, static_cast < int32_t > ( statement.column_count ) );

		for ( uint32_t c = 0; c < statement.column_count; c++ ) {

			const char* name = statement.stmt ? strata_column_name ( statement.stmt, static_cast < int > ( c ) ) : statement.tag;
			int16_t format = format_count == 1 ? formats [ 0 ] : c < format_count ? formats [ c ] : 0;

			put_string ( link, name ? name : "?column?" );
			put32 ( link, 0 );
			put16 ( link, 0 );
			put32 ( link, static_cast < int32_t > ( statement.column_types [ c ] ) );
			put16 ( link, static_cast < int16_t > ( type_size ( statement.column_types [ c ] ) ) );
			put32 ( link, statement.stmt ? statement.column_modifiers [ c ] : -1 );
			put16 ( link, format );

		}

		end_message ( link );

	}

	static void command_complete ( server_connection& link, server_statement& statement, uint64_t rows ) {

		base::text tag;

		if ( !statement.stmt )
			tag.add ( statement.column_count ? "SHOW" : statement.tag );
		else {

			engine::prepared_statement* prepared = statement.stmt->prepared;

			switch ( prepared && prepared->statement ? prepared->statement->kind : sql::STATEMENT_SELECT ) {

				case sql::STATEMENT_SELECT: tag.add ( "SELECT " ).number ( rows ); break;
				case sql::STATEMENT_INSERT: tag.add ( "INSERT 0 " ).signed_number ( strata_changes ( link.db ) ); break;
				case sql::STATEMENT_UPDATE: tag.add ( "UPDATE " ).signed_number ( strata_changes ( link.db ) ); break;
				case sql::STATEMENT_DELETE: tag.add ( "DELETE " ).signed_number ( strata_changes ( link.db ) ); break;
				case sql::STATEMENT_CREATE_TABLE: tag.add ( "CREATE TABLE" ); break;
				case sql::STATEMENT_CREATE_INDEX: tag.add ( "CREATE INDEX" ); break;
				case sql::STATEMENT_DROP_TABLE: tag.add ( "DROP TABLE" ); break;
				case sql::STATEMENT_DROP_INDEX: tag.add ( "DROP INDEX" ); break;
				case sql::STATEMENT_ALTER_TABLE: tag.add ( "ALTER TABLE" ); break;
				case sql::STATEMENT_BEGIN: tag.add ( "BEGIN" ); break;
				case sql::STATEMENT_COMMIT: tag.add ( "COMMIT" ); break;
				case sql::STATEMENT_ROLLBACK: tag.add ( "ROLLBACK" ); break;
				case sql::STATEMENT_EXPLAIN: tag.add ( "EXPLAIN" ); break;
				case sql::STATEMENT_ANALYZE: tag.add ( "ANALYZE" ); break;
				case sql::STATEMENT_VACUUM: tag.add ( "VACUUM" ); break;
				case sql::STATEMENT_SAVEPOINT: tag.add ( "SAVEPOINT" ); break;
				case sql::STATEMENT_RELEASE: tag.add ( "RELEASE" ); break;
				case sql::STATEMENT_CREATE_SCHEMA: tag.add ( "CREATE SCHEMA" ); break;
				case sql::STATEMENT_DROP_SCHEMA: tag.add ( "DROP SCHEMA" ); break;
				default: tag.add ( "OK" ); break;

			}

		}

		link.rolled_back = base::starts_with ( tag.value ( ), "ROLLBACK" );
		begin_message ( link, 'C' );
		put_string ( link, tag.value ( ) );
		end_message ( link );

	}

	static void report ( server_connection& link ) {

		const char* message = strata_error ( link.db );

		send_error ( link, error_code ( message, link.db->database.parse_failed ), message && *message ? message : "the statement failed" );

	}

	static void release_statement ( server_statement* statement ) {

		if ( !statement )
			return;

		if ( statement->stmt )
			strata_finalize ( statement->stmt );

		if ( statement->body )
			base::release ( statement->body );

		base::release ( statement );

	}

	void forget_statements ( server_connection& link ) {

		for ( uint32_t i = 0; i < link.statement_count; i++ )
			release_statement ( link.statements [ i ] );

		link.statement_count = 0;
		link.portal = { };

	}

	static bool fill_statement ( server_connection& link, server_statement& statement, const char* sql, size_t size, const char*& tail ) {

		char show [ 256 ];
		bool handled = false;
		size_t body = 0;
		size_t body_size = 0;
		size_t after = 0;

		if ( dollar_body ( sql, size, body, body_size, after ) ) {

			statement.control = true;
			statement.body = base::make < char > ( body_size + 1 );

			if ( !statement.body )
				return false;

			base::copy ( statement.body, sql + body, body_size );
			statement.body [ body_size ] = 0;
			statement.body_size = body_size;
			base::copy_text ( statement.tag, sizeof ( statement.tag ), "DO" );

			while ( after < size && ( sql [ after ] == ' ' || sql [ after ] == '\r' || sql [ after ] == '\n' || sql [ after ] == '\t' ) )
				after++;

			tail = sql + ( after < size && sql [ after ] == ';' ? after + 1 : after );

			return true;

		}

		control_statement ( sql, size, statement.tag, sizeof ( statement.tag ), show, sizeof ( show ), handled );

		if ( handled ) {

			statement.control = true;
			base::copy_text ( statement.control_text, sizeof ( statement.control_text ), show );
			size_t start = 0;

			statement.column_count = word_at ( sql, size, start, "show" ) ? 1 : 0;
			statement.column_types [ 0 ] = PG_TEXT;
			statement.returns_rows = statement.column_count != 0;

			size_t at = 0;
			char quote = 0;

			while ( at < size && sql [ at ] && ( quote || sql [ at ] != ';' ) ) {

				if ( quote && sql [ at ] == quote )
					quote = 0;
				else if ( !quote && ( sql [ at ] == '\'' || sql [ at ] == '"' ) )
					quote = sql [ at ];

				at++;

			}

			tail = sql + at;

			if ( base::same ( statement.tag, "LISTEN" ) || base::same ( statement.tag, "UNLISTEN" ) || base::same ( statement.tag, "NOTIFY" ) ) {

				size_t start_args = 0;

				word_at ( sql, at, start_args, statement.tag [ 0 ] == 'L' ? "listen" : statement.tag [ 0 ] == 'U' ? "unlisten" : "notify" );

				statement.body = base::make < char > ( at - start_args + 1 );

				if ( !statement.body )
					return false;

				if ( at > start_args )
					base::copy ( statement.body, sql + start_args, at - start_args );

				statement.body [ at - start_args ] = 0;
				statement.body_size = at - start_args;

			}

			return true;

		}

		if ( strata_prepare ( link.db, sql, static_cast < int > ( size ), &statement.stmt, &tail ) != STRATA_OK )
			return false;

		if ( !statement.stmt )
			return true;

		statement.parameter_count = static_cast < uint32_t > ( strata_parameter_count ( statement.stmt ) );
		statement.column_count = static_cast < uint32_t > ( strata_column_count ( statement.stmt ) );

		if ( statement.column_count > SERVER_COLUMNS )
			statement.column_count = SERVER_COLUMNS;

		statement.returns_rows = statement.stmt->prepared && engine::statement_returns_rows ( *statement.stmt->prepared );

		const engine::prepared_select* plan = statement.stmt->prepared ? engine::statement_plan ( *statement.stmt->prepared ) : nullptr;

		for ( uint32_t c = 0; c < statement.column_count; c++ ) {

			bool explain = statement.stmt->prepared && statement.stmt->prepared->statement->kind == sql::STATEMENT_EXPLAIN;

			statement.column_types [ c ] = explain ? ( c < 2 ? PG_INT8 : PG_TEXT ) : column_type ( plan, c );
			statement.column_modifiers [ c ] = explain ? -1 : column_modifier ( plan, c );

		}

		return true;

	}

	static bool aborted_block ( server_connection& link, server_statement& statement ) {

		if ( !link.failed )
			return false;

		sql::statement_kind kind = statement.stmt && statement.stmt->prepared && statement.stmt->prepared->statement ? statement.stmt->prepared->statement->kind : sql::STATEMENT_SELECT;
		bool rollback = statement.control ? base::same ( statement.tag, "ROLLBACK" ) : statement.stmt && kind == sql::STATEMENT_ROLLBACK;
		bool commit = statement.control ? base::same ( statement.tag, "COMMIT" ) : statement.stmt && kind == sql::STATEMENT_COMMIT;

		if ( rollback ) {

			link.failed = false;

			return false;

		}

		if ( commit ) {

			strata_exec ( link.db, "ROLLBACK" );
			link.failed = false;
			link.rolled_back = true;
			begin_message ( link, 'C' );
			put_string ( link, "ROLLBACK" );
			end_message ( link );

			return true;

		}

		send_error ( link, "25P02", "current transaction is aborted, commands ignored until end of transaction block" );

		return true;

	}

	static bool run_control ( server_connection& link, server_statement& statement, bool describe ) {

		if ( base::same ( statement.tag, "SET" ) && statement.control_text [ 0 ] && !engine::set_search ( *link.db->database.schema, statement.control_text ) ) {

			send_error ( link, "42602", "the schema name in search_path is too long" );

			return false;

		}

		if ( statement.body && base::same ( statement.tag, "DO" ) ) {

			base::text problem;

			if ( !run_do_block ( link, statement.body, statement.body_size, problem ) ) {

				send_error ( link, error_code ( problem.value ( ), false ), problem.size ? problem.value ( ) : "the DO block failed" );

				return false;

			}

		}

		if ( base::same ( statement.tag, "LISTEN" ) || base::same ( statement.tag, "UNLISTEN" ) || base::same ( statement.tag, "NOTIFY" ) ) {

			base::text problem;

			if ( !listen_command ( link, statement.tag, statement.body ? statement.body : "", statement.body_size, problem ) ) {

				send_error ( link, base::same ( statement.tag, "NOTIFY" ) && base::find_folded ( problem.value ( ), "payload" ) ? "22023" : "42601", problem.value ( ) );

				return false;

			}

		}

		if ( base::same ( statement.tag, "DISCARD ALL" ) ) {

			forget_statements ( link );
			link.channel_count = 0;
			engine::set_search ( *link.db->database.schema, "public" );

		}

		if ( base::same ( statement.tag, "DEALLOCATE" ) ) {

			if ( base::same_folded ( statement.control_text, "all" ) )
				forget_statements ( link );
			else
				drop_statement ( link, statement.control_text );

		}

		if ( base::same ( statement.tag, "BEGIN" ) ) {

			if ( !strata_in_transaction ( link.db ) && strata_exec ( link.db, "BEGIN" ) != STRATA_OK ) {

				report ( link );

				return false;

			}

		}

		if ( base::same ( statement.tag, "COMMIT" ) || base::same ( statement.tag, "ROLLBACK" ) ) {

			if ( strata_in_transaction ( link.db ) && strata_exec ( link.db, statement.tag ) != STRATA_OK ) {

				report ( link );

				return false;

			}

		}

		if ( statement.column_count ) {

			if ( describe )
				describe_rows ( link, statement, nullptr, 0 );

			base::text value;

			engine::setting_value ( *link.db->database.schema, statement.control_text, value );
			begin_message ( link, 'D' );
			put16 ( link, 1 );
			put32 ( link, static_cast < int32_t > ( value.size ) );
			put_bytes ( link, value.value ( ), value.size );
			end_message ( link );

		}

		command_complete ( link, statement, 1 );

		return true;

	}

	static int step_waiting ( server_connection& link, strata_stmt* stmt, uint64_t rows ) {

		for ( ;; ) {

			int code = strata_step ( stmt );

			if ( code != STRATA_BUSY || rows ) {

				link.busy_since = 0;
				link.ticket = 0;

				return code;

			}

			strata_reset ( stmt );

			engine::database& base_db = link.db->database;

			if ( base_db.read_committed && !base_db.pages.wal.writing )
				storage::end_read ( base_db.pages );

			if ( !wait_busy ( link ) ) {

				link.busy_since = 0;
				link.ticket = 0;

				return STRATA_BUSY;

			}

		}

	}

	static bool send_rows ( server_connection& link, server_statement& statement, const int16_t* formats, uint32_t format_count, uint64_t limit, uint64_t& rows, bool& suspended ) {

		suspended = false;

		for ( ;; ) {

			if ( limit && rows >= limit ) {

				suspended = true;

				return true;

			}

			int code = step_waiting ( link, statement.stmt, rows );

			if ( code == STRATA_DONE )
				return true;

			if ( code != STRATA_ROW ) {

				if ( code == STRATA_BUSY )
					send_error ( link, "55P03", "the database is busy with another writer, try again" );
				else
					report ( link );

				return false;

			}

			const storage::value* row = statement.stmt->row;

			begin_message ( link, 'D' );
			put16 ( link, static_cast < int32_t > ( statement.column_count ) );

			for ( uint32_t c = 0; c < statement.column_count; c++ ) {

				int16_t format = format_count == 1 ? formats [ 0 ] : c < format_count ? formats [ c ] : 0;

				if ( !encode_value ( link, row [ c ], statement.column_types [ c ], format == 1 ) ) {

					link.output.size = link.message_start;
					link.output.data [ link.output.size ] = 0;

					base::text message;

					message.add ( "the value in column " ).number ( c + 1 ).add ( " does not fit its Postgres type" );
					send_error ( link, "22P02", message.value ( ) );

					return false;

				}

			}

			end_message ( link );
			rows++;

			if ( link.output.size > SERVER_INPUT && !flush_output ( link ) )
				return false;

		}

	}

	bool handle_simple ( server_connection& link, const pg_message& message ) {

		const char* sql = reinterpret_cast < const char* > ( message.body );
		size_t size = message.size ? message.size - 1 : 0;
		const char* at = sql;
		const char* end = sql + size;
		bool any = false;

		if ( link.portal.statement && link.portal.statement->stmt )
			strata_reset ( link.portal.statement->stmt );

		drop_statement ( link, "" );
		link.portal = { };

		while ( at < end ) {

			while ( at < end && ( *at == ' ' || *at == '\t' || *at == '\r' || *at == '\n' || *at == ';' ) )
				at++;

			if ( at >= end )
				break;

			server_statement* statement = base::make < server_statement > ( 1 );
			const char* tail = end;

			if ( !statement )
				return false;

			if ( !fill_statement ( link, *statement, at, static_cast < size_t > ( end - at ), tail ) ) {

				report ( link );
				release_statement ( statement );

				break;

			}

			any = true;

			bool fine = true;

			if ( aborted_block ( link, *statement ) )
				fine = !link.failed;
			else if ( statement->control )
				fine = run_control ( link, *statement, true );
			else if ( statement->stmt ) {

				uint64_t rows = 0;
				bool suspended = false;

				if ( statement->returns_rows )
					describe_rows ( link, *statement, nullptr, 0 );

				fine = send_rows ( link, *statement, nullptr, 0, 0, rows, suspended );

				if ( fine )
					command_complete ( link, *statement, rows );

			}

			release_statement ( statement );
			at = tail > at ? tail : end;

			if ( !fine )
				break;

		}

		if ( !any ) {

			begin_message ( link, 'I' );
			end_message ( link );

		}

		send_ready ( link );

		return true;

	}

	static server_statement* find_statement ( server_connection& link, const char* name, uint32_t* index ) {

		for ( uint32_t i = 0; i < link.statement_count; i++ ) {

			if ( base::same ( link.statements [ i ]->name, name ) ) {

				if ( index )
					*index = i;

				return link.statements [ i ];

			}

		}

		return nullptr;

	}

	static void drop_statement ( server_connection& link, const char* name ) {

		uint32_t index = 0;
		server_statement* found = find_statement ( link, name, &index );

		if ( !found )
			return;

		if ( link.portal.statement == found )
			link.portal = { };

		release_statement ( found );
		link.statements [ index ] = link.statements [ --link.statement_count ];

	}

	static bool parse_message ( server_connection& link, pg_reader& reader ) {

		const char* name = read_string ( reader );
		const char* sql = read_string ( reader );
		int32_t declared = read16 ( reader );

		if ( reader.failed || !name || !sql || declared < 0 ) {

			send_error ( link, "08P01", "a Parse message is malformed" );

			return false;

		}

		drop_statement ( link, name );

		if ( link.statement_count == SERVER_STATEMENTS ) {

			send_error ( link, "53000", "too many prepared statements on this connection" );

			return false;

		}

		server_statement* statement = base::make < server_statement > ( 1 );

		if ( !statement )
			return false;

		base::copy_text ( statement->name, sizeof ( statement->name ), name );

		for ( int32_t i = 0; i < declared && i < SERVER_PARAMETERS; i++ )
			statement->parameter_types [ i ] = static_cast < uint32_t > ( read32 ( reader ) );

		const char* tail = nullptr;

		if ( !fill_statement ( link, *statement, sql, base::length ( sql ), tail ) ) {

			report ( link );
			release_statement ( statement );

			return false;

		}

		for ( uint32_t i = 0; i < statement->parameter_count && i < SERVER_PARAMETERS; i++ )
			if ( !statement->parameter_types [ i ] )
				statement->parameter_types [ i ] = PG_TEXT;

		link.statements [ link.statement_count++ ] = statement;
		begin_message ( link, '1' );
		end_message ( link );

		return true;

	}

	static bool bind_message ( server_connection& link, pg_reader& reader ) {

		const char* portal = read_string ( reader );
		const char* name = read_string ( reader );
		server_statement* statement = name ? find_statement ( link, name, nullptr ) : nullptr;

		if ( link.portal.statement && link.portal.statement->stmt )
			strata_reset ( link.portal.statement->stmt );

		link.portal = { };

		if ( !statement ) {

			send_error ( link, "26000", "the prepared statement does not exist" );

			return false;

		}

		int32_t format_count = read16 ( reader );
		int16_t formats [ SERVER_PARAMETERS ];

		for ( int32_t i = 0; i < format_count && i < SERVER_PARAMETERS; i++ )
			formats [ i ] = read16 ( reader );

		int32_t count = read16 ( reader );
		base::arena memory;
		strata_value* values = count > 0 ? memory.make < strata_value > ( static_cast < size_t > ( count ) ) : nullptr;

		for ( int32_t i = 0; i < count && !reader.failed; i++ ) {

			int32_t size = read32 ( reader );
			const uint8_t* data = size > 0 ? read_span ( reader, static_cast < size_t > ( size ) ) : reinterpret_cast < const uint8_t* > ( "" );
			int16_t format = format_count == 1 ? formats [ 0 ] : i < format_count ? formats [ i ] : 0;
			uint32_t type = i < SERVER_PARAMETERS ? statement->parameter_types [ i ] : PG_TEXT;

			if ( !values || !decode_parameter ( memory, data, size, type, format == 1, values [ i ] ) ) {

				send_error ( link, "22P03", "a parameter could not be read" );

				return false;

			}

		}

		link.portal = { };
		base::copy_text ( link.portal.name, sizeof ( link.portal.name ), portal ? portal : "" );
		link.portal.statement = statement;
		link.portal.format_count = static_cast < uint32_t > ( read16 ( reader ) );

		for ( uint32_t i = 0; i < link.portal.format_count && i < SERVER_COLUMNS; i++ )
			link.portal.formats [ i ] = read16 ( reader );

		if ( reader.failed ) {

			send_error ( link, "08P01", "a Bind message is malformed" );

			return false;

		}

		if ( statement->stmt ) {

			strata_reset ( statement->stmt );
			strata_clear_bindings ( statement->stmt );

			if ( count > 0 && strata_bind_values ( statement->stmt, values, count ) != STRATA_OK ) {

				report ( link );

				return false;

			}

		}

		begin_message ( link, '2' );
		end_message ( link );

		return true;

	}

	static bool describe_message ( server_connection& link, pg_reader& reader ) {

		uint8_t kind = read8 ( reader );
		const char* name = read_string ( reader );

		if ( kind == 'S' ) {

			server_statement* statement = name ? find_statement ( link, name, nullptr ) : nullptr;

			if ( !statement ) {

				send_error ( link, "26000", "the prepared statement does not exist" );

				return false;

			}

			begin_message ( link, 't' );
			put16 ( link, static_cast < int32_t > ( statement->parameter_count ) );

			for ( uint32_t i = 0; i < statement->parameter_count; i++ )
				put32 ( link, static_cast < int32_t > ( i < SERVER_PARAMETERS ? statement->parameter_types [ i ] : PG_TEXT ) );

			end_message ( link );

			if ( statement->returns_rows )
				describe_rows ( link, *statement, nullptr, 0 );
			else {

				begin_message ( link, 'n' );
				end_message ( link );

			}

			return true;

		}

		if ( !link.portal.statement ) {

			send_error ( link, "34000", "the portal does not exist" );

			return false;

		}

		if ( link.portal.statement->returns_rows )
			describe_rows ( link, *link.portal.statement, link.portal.formats, link.portal.format_count );
		else {

			begin_message ( link, 'n' );
			end_message ( link );

		}

		return true;

	}

	static bool execute_message ( server_connection& link, pg_reader& reader ) {

		read_string ( reader );

		int32_t limit = read32 ( reader );
		server_portal& portal = link.portal;

		if ( !portal.statement ) {

			send_error ( link, "34000", "the portal does not exist" );

			return false;

		}

		server_statement& statement = *portal.statement;

		if ( aborted_block ( link, statement ) )
			return !link.failed;

		if ( statement.control )
			return run_control ( link, statement, false );

		if ( !statement.stmt ) {

			begin_message ( link, 'I' );
			end_message ( link );

			return true;

		}

		bool suspended = false;

		if ( !send_rows ( link, statement, portal.formats, portal.format_count, limit > 0 ? portal.rows + static_cast < uint64_t > ( limit ) : 0, portal.rows, suspended ) )
			return false;

		if ( suspended ) {

			begin_message ( link, 's' );
			end_message ( link );

			return true;

		}

		command_complete ( link, statement, portal.rows );
		portal.finished = true;

		return true;

	}

	bool handle_extended ( server_connection& link, const pg_message& message ) {

		if ( message.type == 'S' ) {

			link.skipping = false;
			send_ready ( link );

			return true;

		}

		if ( message.type == 'H' )
			return true;

		if ( link.skipping )
			return true;

		pg_reader reader = { message.body, message.body + message.size, false };
		bool fine = true;

		switch ( message.type ) {

			case 'P':
				fine = parse_message ( link, reader );
				break;

			case 'B':
				fine = bind_message ( link, reader );
				break;

			case 'D':
				fine = describe_message ( link, reader );
				break;

			case 'E':
				fine = execute_message ( link, reader );
				break;

			case 'C': {

				uint8_t kind = read8 ( reader );
				const char* name = read_string ( reader );

				if ( kind == 'S' && name )
					drop_statement ( link, name );
				else
					link.portal = { };

				begin_message ( link, '3' );
				end_message ( link );

				break;

			}

			default:
				break;

		}

		if ( !fine )
			link.skipping = true;

		return !link.closed;

	}

}
