#include "server/server.hh"
#include "base/file/folder.hh"

namespace server {

	static void grants_path ( const char* path, base::text& out ) {

		out.clear ( );
		out.add ( path ).add ( ".grants" );

	}

	bool load_grants ( server_state& state ) {

		base::text path;
		base::text content;

		state.grant_count = 0;
		grants_path ( state.settings.path, path );

		if ( !base::path_exists ( path.value ( ) ) || !base::read_whole ( path.value ( ), content ) )
			return false;

		const char* at = content.value ( );

		while ( *at && state.grant_count < SERVER_GRANTS ) {

			server_grant& grant = state.grants [ state.grant_count ];
			size_t length = 0;

			grant = { };

			while ( *at && *at != '\t' && *at != '\n' && length + 1 < SERVER_NAME_ROOM )
				grant.grantee [ length++ ] = *at++;

			if ( *at++ != '\t' )
				return false;

			length = 0;

			while ( *at && *at != '\t' && *at != '\n' && length + 1 < SERVER_OBJECT_ROOM )
				grant.object [ length++ ] = *at++;

			if ( *at++ != '\t' )
				return false;

			while ( *at >= '0' && *at <= '9' )
				grant.privileges = grant.privileges * 10 + static_cast < uint32_t > ( *at++ - '0' );

			while ( *at == '\r' || *at == '\n' )
				at++;

			if ( grant.grantee [ 0 ] && grant.object [ 0 ] && grant.privileges )
				state.grant_count++;

		}

		return true;

	}

	bool save_grants ( server_state& state ) {

		base::text path;
		base::text written;

		grants_path ( state.settings.path, path );

		for ( uint32_t i = 0; i < state.grant_count; i++ )
			written.add ( state.grants [ i ].grantee ).add ( '\t' ).add ( state.grants [ i ].object ).add ( '\t' ).number ( state.grants [ i ].privileges ).add ( '\n' );

		return base::save_private_file ( path.value ( ), written.value ( ), written.size );

	}

	static void need ( access_list& list, const char* name, uint32_t privilege ) {

		if ( !name )
			return;

		for ( uint32_t i = 0; i < list.cte_count; i++ )
			if ( base::same_folded ( list.ctes [ i ], name ) )
				return;

		for ( uint32_t i = 0; i < list.count; i++ ) {

			if ( base::same_folded ( list.names [ i ], name ) ) {

				list.privileges [ i ] |= privilege;

				return;

			}

		}

		if ( list.count == SERVER_ACCESS_NAMES ) {

			list.overflow = true;

			return;

		}

		list.names [ list.count ] = name;
		list.privileges [ list.count++ ] = privilege;

	}

	static void walk_select ( access_list& list, const sql::select_statement* item, uint32_t depth );

	static void walk_expression ( access_list& list, const sql::expression* item, uint32_t depth ) {

		if ( !item )
			return;

		if ( depth > 256 ) {

			list.overflow = true;

			return;

		}

		if ( item->query )
			walk_select ( list, item->query, depth + 1 );

		walk_expression ( list, item->left, depth + 1 );
		walk_expression ( list, item->right, depth + 1 );
		walk_expression ( list, item->third, depth + 1 );

		for ( uint32_t i = 0; i < item->count; i++ )
			walk_expression ( list, item->list [ i ], depth + 1 );

		if ( item->window ) {

			for ( uint32_t i = 0; i < item->window->partition_count; i++ )
				walk_expression ( list, item->window->partition [ i ], depth + 1 );

			for ( uint32_t i = 0; i < item->window->order_count; i++ )
				walk_expression ( list, item->window->order [ i ].value, depth + 1 );

		}

	}

	static void walk_select ( access_list& list, const sql::select_statement* item, uint32_t depth ) {

		if ( depth > 256 ) {

			list.overflow = true;

			return;

		}

		for ( const sql::select_statement* core = item; core; core = core->next ) {

			for ( uint32_t i = 0; i < core->with_count; i++ ) {

				if ( list.cte_count < SERVER_ACCESS_NAMES )
					list.ctes [ list.cte_count++ ] = core->with [ i ].name;
				else
					list.overflow = true;

			}

			for ( uint32_t i = 0; i < core->with_count; i++ )
				walk_select ( list, core->with [ i ].query, depth + 1 );

			for ( uint32_t i = 0; i < core->source_count; i++ ) {

				const sql::table_source& source = core->sources [ i ];

				if ( source.query )
					walk_select ( list, source.query, depth + 1 );
				else
					need ( list, source.name, ACCESS_SELECT );

				walk_expression ( list, source.on, depth + 1 );

			}

			for ( uint32_t i = 0; i < core->column_count; i++ )
				walk_expression ( list, core->columns [ i ].value, depth + 1 );

			walk_expression ( list, core->where, depth + 1 );
			walk_expression ( list, core->having, depth + 1 );
			walk_expression ( list, core->limit, depth + 1 );
			walk_expression ( list, core->offset, depth + 1 );

			for ( uint32_t i = 0; i < core->group_count; i++ )
				walk_expression ( list, core->group [ i ], depth + 1 );

			for ( uint32_t i = 0; i < core->order_count; i++ )
				walk_expression ( list, core->order [ i ].value, depth + 1 );

			for ( uint32_t i = 0; core->values && i < core->value_rows * core->value_columns; i++ )
				walk_expression ( list, core->values [ i ], depth + 1 );

		}

	}

	static void walk_returning ( access_list& list, const char* table, const sql::returning_clause& clause ) {

		if ( !clause.count )
			return;

		need ( list, table, ACCESS_SELECT );

		for ( uint32_t i = 0; i < clause.count; i++ )
			walk_expression ( list, clause.columns [ i ].value, 0 );

	}

	static void walk_statement ( access_list& list, const sql::statement* item ) {

		switch ( item->kind ) {

			case sql::STATEMENT_SELECT:
				walk_select ( list, item->select, 0 );
				break;

			case sql::STATEMENT_INSERT: {

				const sql::insert_statement* insert = item->insert;

				need ( list, insert->table, ACCESS_INSERT | ( insert->conflict == sql::CONFLICT_UPDATE || insert->conflict == sql::CONFLICT_REPLACE ? ACCESS_UPDATE : 0 ) );
				walk_select ( list, insert->query, 0 );

				for ( uint32_t i = 0; i < insert->update_count; i++ )
					walk_expression ( list, insert->updates [ i ].value, 0 );

				walk_expression ( list, insert->update_where, 0 );
				walk_returning ( list, insert->table, insert->returning );
				break;

			}

			case sql::STATEMENT_UPDATE: {

				const sql::update_statement* update = item->update;

				need ( list, update->table, ACCESS_UPDATE | ( update->where ? ACCESS_SELECT : 0 ) );

				for ( uint32_t i = 0; i < update->assignment_count; i++ )
					walk_expression ( list, update->assignments [ i ].value, 0 );

				walk_expression ( list, update->where, 0 );
				walk_returning ( list, update->table, update->returning );
				break;

			}

			case sql::STATEMENT_DELETE: {

				const sql::delete_statement* remove = item->remove;

				need ( list, remove->table, ACCESS_DELETE | ( remove->where ? ACCESS_SELECT : 0 ) );
				walk_expression ( list, remove->where, 0 );
				walk_returning ( list, remove->table, remove->returning );
				break;

			}

			case sql::STATEMENT_EXPLAIN:
				if ( item->inner )
					walk_statement ( list, item->inner );

				break;

			case sql::STATEMENT_CREATE_TABLE:
				need ( list, "*", ACCESS_CREATE );

				if ( item->create_table && item->create_table->query )
					walk_select ( list, item->create_table->query, 0 );

				break;

			case sql::STATEMENT_CREATE_VIEW:
				need ( list, "*", ACCESS_CREATE );

				if ( item->create_view && item->create_view->query )
					walk_select ( list, item->create_view->query, 0 );

				break;

			case sql::STATEMENT_CREATE_INDEX:
			case sql::STATEMENT_DROP_TABLE:
			case sql::STATEMENT_DROP_INDEX:
			case sql::STATEMENT_ALTER_TABLE:
			case sql::STATEMENT_CREATE_SCHEMA:
			case sql::STATEMENT_DROP_SCHEMA:
			case sql::STATEMENT_DROP_VIEW:
			case sql::STATEMENT_CREATE_SEQUENCE:
			case sql::STATEMENT_ALTER_SEQUENCE:
			case sql::STATEMENT_DROP_SEQUENCE:
			case sql::STATEMENT_VACUUM:
			case sql::STATEMENT_ANALYZE:
				need ( list, "*", ACCESS_CREATE );
				break;

			default:
				break;

		}

	}

	static bool granted ( server_state& state, const char* user, const char* object, uint32_t privilege ) {

		uint32_t have = 0;

		for ( uint32_t i = 0; i < state.grant_count; i++ ) {

			const server_grant& grant = state.grants [ i ];

			if ( ( base::same_folded ( grant.grantee, user ) || base::same ( grant.grantee, "public" ) ) && ( base::same_folded ( grant.object, object ) || ( base::same ( grant.object, "*" ) && privilege == ACCESS_CREATE ) ) )
				have |= grant.privileges;

		}

		return ( have & privilege ) == privilege;

	}

	static const char* privilege_name ( uint32_t missing ) {

		return missing & ACCESS_SELECT ? "SELECT" : missing & ACCESS_INSERT ? "INSERT" : missing & ACCESS_UPDATE ? "UPDATE" : missing & ACCESS_DELETE ? "DELETE" : "CREATE";

	}

	bool access_allowed ( server_connection& link, const sql::statement* statement, base::text& problem ) {

		if ( !link.limited || !statement )
			return true;

		access_list list = { };

		walk_statement ( list, statement );

		if ( list.overflow ) {

			problem.add ( "permission denied: the statement is too large to check" );

			return false;

		}

		for ( uint32_t i = 0; i < list.count; i++ ) {

			const char* name = list.names [ i ];
			uint32_t privilege = list.privileges [ i ];

			if ( base::same ( name, "*" ) ) {

				if ( !granted ( *link.owner, link.user, "*", ACCESS_CREATE ) ) {

					problem.add ( "permission denied to change the schema, ask an administrator to GRANT CREATE ON SCHEMA public TO " ).add ( link.user );

					return false;

				}

				continue;

			}

			if ( privilege == ACCESS_SELECT && ( base::starts_with ( name, "pg_" ) || base::starts_with ( name, "information_schema." ) ) )
				continue;

			engine::table_info* table = engine::find_table ( *link.db->database.schema, name );

			if ( !table )
				continue;

			for ( uint32_t bit = 1; bit < ACCESS_CREATE; bit <<= 1 ) {

				if ( !( privilege & bit ) || granted ( *link.owner, link.user, table->name, bit ) )
					continue;

				problem.add ( "permission denied for table " ).add ( table->name ).add ( ": " ).add ( link.user ).add ( " has no " ).add ( privilege_name ( bit ) ).add ( " privilege" );

				return false;

			}

		}

		return true;

	}

	static size_t skip ( const char* text, size_t at, size_t size ) {

		while ( at < size && ( text [ at ] == ' ' || text [ at ] == '\t' || text [ at ] == '\r' || text [ at ] == '\n' ) )
			at++;

		return at;

	}

	static bool word_char ( char c ) {

		return ( c >= 'a' && c <= 'z' ) || ( c >= 'A' && c <= 'Z' ) || ( c >= '0' && c <= '9' ) || c == '_' || c == '$';

	}

	static bool take_word ( const char* text, size_t& at, size_t size, char* out, size_t room ) {

		size_t length = 0;

		at = skip ( text, at, size );

		for ( ;; ) {

			if ( at < size && text [ at ] == '"' ) {

				at++;

				while ( at < size && !( text [ at ] == '"' && !( at + 1 < size && text [ at + 1 ] == '"' ) ) ) {

					if ( text [ at ] == '"' )
						at++;

					if ( length + 1 >= room )
						return false;

					out [ length++ ] = text [ at++ ];

				}

				if ( at >= size )
					return false;

				at++;

			} else {

				size_t start = length;

				while ( at < size && word_char ( text [ at ] ) ) {

					if ( length + 1 >= room )
						return false;

					out [ length++ ] = base::lower ( text [ at++ ] );

				}

				if ( length == start )
					break;

			}

			if ( at < size && text [ at ] == '.' && length + 1 < room ) {

				out [ length++ ] = '.';
				at++;

				continue;

			}

			break;

		}

		out [ length ] = 0;

		return length > 0;

	}

	static bool accept ( const char* text, size_t& at, size_t size, const char* word ) {

		size_t probe = at;
		char found [ SERVER_OBJECT_ROOM ];

		if ( !take_word ( text, probe, size, found, sizeof ( found ) ) || !base::same_folded ( found, word ) )
			return false;

		at = probe;

		return true;

	}

	static bool accept_symbol ( const char* text, size_t& at, size_t size, char symbol ) {

		at = skip ( text, at, size );

		if ( at < size && text [ at ] == symbol ) {

			at++;

			return true;

		}

		return false;

	}

	static bool take_string ( const char* text, size_t& at, size_t size, base::text& out ) {

		at = skip ( text, at, size );

		if ( at >= size || text [ at ] != '\'' )
			return false;

		at++;

		while ( at < size ) {

			if ( text [ at ] == '\'' ) {

				if ( at + 1 < size && text [ at + 1 ] == '\'' )
					at++;
				else
					break;

			}

			out.add ( text [ at++ ] );

		}

		if ( at >= size )
			return false;

		at++;

		return true;

	}

	static int32_t find_role ( server_state& state, const char* name ) {

		for ( uint32_t i = 0; i < state.user_count; i++ )
			if ( base::same_folded ( state.users [ i ].name, name ) )
				return static_cast < int32_t > ( i );

		return -1;

	}

	static bool role_options ( const char* text, size_t& at, size_t size, server_user& role, bool& password_set, base::text& problem ) {

		accept ( text, at, size, "with" );

		for ( ;; ) {

			at = skip ( text, at, size );

			if ( at >= size || text [ at ] == ';' )
				return true;

			if ( accept ( text, at, size, "superuser" ) )
				role.limited = false;
			else if ( accept ( text, at, size, "nosuperuser" ) )
				role.limited = true;
			else if ( accept ( text, at, size, "password" ) ) {

				base::text password;

				if ( accept ( text, at, size, "null" ) )
					continue;

				if ( !take_string ( text, at, size, password ) || !password.size ) {

					problem.add ( "PASSWORD needs a quoted password" );

					return false;

				}

				server_user made = { };

				if ( !make_user ( role.name, password.value ( ), made ) ) {

					problem.add ( "the password could not be hashed" );

					return false;

				}

				made.limited = role.limited;
				role = made;
				password_set = true;

			} else if ( accept ( text, at, size, "connection" ) ) {

				char ignored [ SERVER_OBJECT_ROOM ];

				if ( !accept ( text, at, size, "limit" ) || !take_word ( text, at, size, ignored, sizeof ( ignored ) ) ) {

					problem.add ( "expected CONNECTION LIMIT and a number" );

					return false;

				}

			} else if ( accept ( text, at, size, "valid" ) ) {

				base::text ignored;

				if ( !accept ( text, at, size, "until" ) || !take_string ( text, at, size, ignored ) ) {

					problem.add ( "expected VALID UNTIL and a quoted time" );

					return false;

				}

			} else {

				char word [ SERVER_OBJECT_ROOM ];
				static const char* const quiet [ ] = { "login", "nologin", "inherit", "noinherit", "createdb", "nocreatedb", "createrole", "nocreaterole", "replication", "noreplication", "bypassrls", "nobypassrls" };
				bool known = false;

				if ( !take_word ( text, at, size, word, sizeof ( word ) ) ) {

					problem.add ( "this role option is not understood" );

					return false;

				}

				for ( const char* option : quiet )
					known = known || base::same_folded ( word, option );

				if ( !known ) {

					problem.add ( "the role option " ).add ( word ).add ( " is not supported" );

					return false;

				}

			}

		}

	}

	static bool role_command ( server_connection& link, const char* tag, const char* text, size_t size, base::text& problem, const char*& code ) {

		server_state& state = *link.owner;
		size_t at = 0;
		char name [ SERVER_NAME_ROOM ];
		char word [ SERVER_OBJECT_ROOM ];

		take_word ( text, at, size, word, sizeof ( word ) );
		take_word ( text, at, size, word, sizeof ( word ) );

		if ( base::same ( tag, "DROP ROLE" ) ) {

			bool quiet = false;

			if ( accept ( text, at, size, "if" ) ) {

				if ( !accept ( text, at, size, "exists" ) ) {

					problem.add ( "expected IF EXISTS" );

					return false;

				}

				quiet = true;

			}

			do {

				if ( !take_word ( text, at, size, name, sizeof ( name ) ) ) {

					problem.add ( "expected a role name" );

					return false;

				}

				int32_t found = find_role ( state, name );

				if ( found < 0 ) {

					if ( quiet )
						continue;

					code = "42704";
					problem.add ( "role \"" ).add ( name ).add ( "\" does not exist" );

					return false;

				}

				if ( base::same_folded ( state.users [ found ].name, link.user ) ) {

					code = "55006";
					problem.add ( "the current user cannot be dropped" );

					return false;

				}

				state.users [ found ] = state.users [ --state.user_count ];

				uint32_t kept = 0;

				for ( uint32_t i = 0; i < state.grant_count; i++ )
					if ( !base::same_folded ( state.grants [ i ].grantee, name ) )
						state.grants [ kept++ ] = state.grants [ i ];

				state.grant_count = kept;

			} while ( accept_symbol ( text, at, size, ',' ) );

			if ( !save_users ( state.settings.path, state.users, state.user_count ) || !save_grants ( state ) ) {

				problem.add ( "the roles could not be saved" );

				return false;

			}

			return true;

		}

		if ( !take_word ( text, at, size, name, sizeof ( name ) ) ) {

			problem.add ( "expected a role name" );

			return false;

		}

		int32_t found = find_role ( state, name );
		bool creating = base::same ( tag, "CREATE ROLE" );

		if ( creating && found >= 0 ) {

			code = "42710";
			problem.add ( "role \"" ).add ( name ).add ( "\" already exists" );

			return false;

		}

		if ( !creating && found < 0 ) {

			code = "42704";
			problem.add ( "role \"" ).add ( name ).add ( "\" does not exist" );

			return false;

		}

		if ( creating && state.user_count == SERVER_USERS ) {

			problem.add ( "the server already has the most roles it can hold" );

			return false;

		}

		server_user role = creating ? server_user { } : state.users [ found ];
		bool password_set = false;

		if ( creating ) {

			uint8_t secret [ 24 ];
			base::text random;

			base::os::random_bytes ( secret, sizeof ( secret ) );

			for ( uint8_t byte : secret )
				random.hex ( byte, 2 );

			if ( !make_user ( name, random.value ( ), role ) ) {

				problem.add ( "the role could not be made" );

				return false;

			}

			role.limited = true;

		}

		if ( !role_options ( text, at, size, role, password_set, problem ) )
			return false;

		if ( creating )
			state.users [ state.user_count++ ] = role;
		else
			state.users [ found ] = role;

		if ( !save_users ( state.settings.path, state.users, state.user_count ) ) {

			problem.add ( "the roles could not be saved" );

			return false;

		}

		return true;

	}

	static uint32_t privilege_bits ( const char* word ) {

		if ( base::same_folded ( word, "select" ) )
			return ACCESS_SELECT;

		if ( base::same_folded ( word, "insert" ) )
			return ACCESS_INSERT;

		if ( base::same_folded ( word, "update" ) )
			return ACCESS_UPDATE;

		if ( base::same_folded ( word, "delete" ) || base::same_folded ( word, "truncate" ) )
			return ACCESS_DELETE;

		if ( base::same_folded ( word, "create" ) )
			return ACCESS_CREATE;

		if ( base::same_folded ( word, "all" ) )
			return ACCESS_ALL;

		return 0;

	}

	static void change_grant ( server_state& state, const char* grantee, const char* object, uint32_t bits, bool adding ) {

		for ( uint32_t i = 0; i < state.grant_count; i++ ) {

			server_grant& grant = state.grants [ i ];

			if ( !base::same_folded ( grant.grantee, grantee ) || !base::same_folded ( grant.object, object ) )
				continue;

			grant.privileges = adding ? grant.privileges | bits : grant.privileges & ~bits;

			if ( !grant.privileges )
				state.grants [ i ] = state.grants [ --state.grant_count ];

			return;

		}

		if ( !adding || state.grant_count == SERVER_GRANTS )
			return;

		server_grant& made = state.grants [ state.grant_count++ ];

		made = { };
		base::copy_text ( made.grantee, sizeof ( made.grantee ), grantee );
		base::copy_text ( made.object, sizeof ( made.object ), object );
		made.privileges = bits;

	}

	static bool grant_command ( server_connection& link, bool adding, const char* text, size_t size, base::text& problem, const char*& code ) {

		server_state& state = *link.owner;
		engine::catalog& schema = *link.db->database.schema;
		size_t at = 0;
		char word [ SERVER_OBJECT_ROOM ];
		uint32_t bits = 0;
		const char* objects [ 512 ];
		uint32_t object_count = 0;

		take_word ( text, at, size, word, sizeof ( word ) );

		if ( !adding && accept ( text, at, size, "grant" ) && !( accept ( text, at, size, "option" ) && accept ( text, at, size, "for" ) ) ) {

			problem.add ( "expected GRANT OPTION FOR" );

			return false;

		}

		do {

			if ( !take_word ( text, at, size, word, sizeof ( word ) ) || base::same_folded ( word, "on" ) ) {

				problem.add ( "expected a privilege like SELECT" );

				return false;

			}

			bits |= privilege_bits ( word );

			if ( base::same_folded ( word, "all" ) )
				accept ( text, at, size, "privileges" );

			at = skip ( text, at, size );

			if ( at < size && text [ at ] == '(' ) {

				problem.add ( "column privileges are not supported, grant on the whole table" );

				return false;

			}

		} while ( accept_symbol ( text, at, size, ',' ) );

		if ( !accept ( text, at, size, "on" ) ) {

			problem.add ( "expected ON" );

			return false;

		}

		if ( accept ( text, at, size, "database" ) || accept ( text, at, size, "schema" ) ) {

			if ( !take_word ( text, at, size, word, sizeof ( word ) ) ) {

				problem.add ( "expected a database or schema name" );

				return false;

			}

			while ( accept_symbol ( text, at, size, ',' ) )
				take_word ( text, at, size, word, sizeof ( word ) );

			bits &= ACCESS_CREATE;
			objects [ object_count++ ] = "*";

		} else if ( accept ( text, at, size, "all" ) ) {

			if ( !accept ( text, at, size, "tables" ) || !accept ( text, at, size, "in" ) || !accept ( text, at, size, "schema" ) || !take_word ( text, at, size, word, sizeof ( word ) ) ) {

				problem.add ( "expected ALL TABLES IN SCHEMA and a schema name" );

				return false;

			}

			bool public_schema = base::same_folded ( word, "public" );
			size_t prefix = base::length ( word );

			for ( uint32_t t = 0; t < schema.table_count && object_count < 512; t++ ) {

				const char* table = schema.tables [ t ].name;
				bool dotted = false;

				for ( const char* c = table; *c; c++ )
					dotted = dotted || *c == '.';

				if ( base::starts_with ( table, "strata_" ) )
					continue;

				if ( public_schema ? !dotted : ( base::same_folded_size ( table, word, prefix ) && table [ prefix ] == '.' ) )
					objects [ object_count++ ] = table;

			}

			bits &= ~ACCESS_CREATE;

		} else {

			accept ( text, at, size, "table" );

			do {

				if ( object_count == 512 || !take_word ( text, at, size, word, sizeof ( word ) ) ) {

					problem.add ( "expected a table name" );

					return false;

				}

				engine::table_info* table = engine::find_table ( schema, word );

				if ( !table ) {

					code = "42P01";
					problem.add ( "relation \"" ).add ( word ).add ( "\" does not exist" );

					return false;

				}

				objects [ object_count++ ] = table->name;

			} while ( accept_symbol ( text, at, size, ',' ) );

			bits &= ~ACCESS_CREATE;

		}

		if ( !accept ( text, at, size, adding ? "to" : "from" ) ) {

			problem.add ( adding ? "expected TO and a role" : "expected FROM and a role" );

			return false;

		}

		do {

			char grantee [ SERVER_NAME_ROOM ];

			if ( !take_word ( text, at, size, grantee, sizeof ( grantee ) ) ) {

				problem.add ( "expected a role name" );

				return false;

			}

			if ( !base::same_folded ( grantee, "public" ) && find_role ( state, grantee ) < 0 ) {

				code = "42704";
				problem.add ( "role \"" ).add ( grantee ).add ( "\" does not exist" );

				return false;

			}

			for ( uint32_t i = 0; i < object_count; i++ )
				change_grant ( state, grantee, objects [ i ], bits, adding );

		} while ( accept_symbol ( text, at, size, ',' ) );

		if ( !save_grants ( state ) ) {

			problem.add ( "the grants could not be saved" );

			return false;

		}

		return true;

	}

	void grant_owner ( server_connection& link, const char* name ) {

		if ( !link.limited || !name )
			return;

		engine::table_info* table = engine::find_table ( *link.db->database.schema, name );

		if ( !table )
			return;

		change_grant ( *link.owner, link.user, table->name, ACCESS_ALL & ~ACCESS_CREATE, true );
		save_grants ( *link.owner );

	}

	bool access_command ( server_connection& link, const char* tag, const char* text, size_t size, base::text& problem, const char*& code ) {

		code = "42601";

		if ( link.limited ) {

			code = "42501";
			problem.add ( "permission denied: only a superuser can change roles and grants" );

			return false;

		}

		if ( base::same ( tag, "GRANT" ) || base::same ( tag, "REVOKE" ) )
			return grant_command ( link, base::same ( tag, "GRANT" ), text, size, problem, code );

		return role_command ( link, tag, text, size, problem, code );

	}

}
