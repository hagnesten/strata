#include "studio/app/studio.hh"

namespace studio {

	static const char* const suggest_keywords [ ] = {

		"SELECT", "FROM", "WHERE", "GROUP", "BY", "ORDER", "HAVING", "LIMIT", "OFFSET", "JOIN", "LEFT", "RIGHT", "FULL",
		"INNER", "CROSS", "NATURAL", "OUTER", "ON", "USING", "AS", "AND", "OR", "NOT", "NULL", "IS", "IN", "LIKE", "GLOB",
		"MATCH", "BETWEEN", "EXISTS", "CASE", "WHEN", "THEN", "ELSE", "END", "DISTINCT", "UNION", "INTERSECT", "EXCEPT",
		"WITH", "RECURSIVE", "INSERT", "INTO", "VALUES", "UPDATE", "SET", "DELETE", "CREATE", "TABLE", "INDEX", "FULLTEXT",
		"UNIQUE", "PRIMARY", "KEY", "DEFAULT", "CHECK", "REFERENCES", "FOREIGN", "CASCADE", "DROP", "ALTER", "ADD", "COLUMN",
		"RENAME", "TO", "BEGIN", "COMMIT", "ROLLBACK", "SAVEPOINT", "RELEASE", "EXPLAIN", "QUERY", "PLAN", "ASC", "DESC",
		"INTEGER", "TEXT", "REAL", "BLOB", "NUMERIC", "COLLATE", "NOCASE", "GENERATED", "ALWAYS", "STORED", "OVER",
		"PARTITION", "ROWS", "RANGE", "PRECEDING", "FOLLOWING", "CURRENT", "UNBOUNDED", "VACUUM", "ANALYZE", "CAST",
		"CONFLICT", "DO", "NOTHING", "REPLACE", "IGNORE", "TRUE", "FALSE", "CURRENT_TIMESTAMP", "CURRENT_DATE",

	};

	static const char* const suggest_functions [ ] = {

		"abs", "avg", "bm25", "char", "coalesce", "concat", "concat_ws", "count", "cume_dist", "date", "datetime",
		"dense_rank", "first_value", "glob", "group_concat", "highlight", "hex", "ifnull", "iif", "instr", "json",
		"json_array", "json_extract", "json_group_array", "json_group_object", "json_object", "json_type", "json_valid",
		"julianday", "lag", "last_insert_rowid", "last_value", "lead", "length", "like", "lower", "ltrim", "max", "min",
		"nth_value", "ntile", "nullif", "percent_rank", "printf", "quote", "random", "randomblob", "rank", "replace",
		"round", "row_number", "rtrim", "sign", "snippet", "strftime", "substr", "sum", "time", "total", "trim",
		"typeof", "unicode", "unixepoch", "upper", "zeroblob",

	};

	static bool starts_folded ( const char* word, const char* prefix, size_t size ) {

		for ( size_t i = 0; i < size; i++ )
			if ( !word [ i ] || base::lower ( word [ i ] ) != base::lower ( prefix [ i ] ) )
				return false;

		return true;

	}

	static bool same_word ( const char* word, const char* text, size_t size ) {

		return base::length ( word ) == size && starts_folded ( word, text, size );

	}

	static const engine::table_info* table_for ( studio_state& app, const char* qualifier, size_t size ) {

		engine::catalog& schema = *app.db->schema;

		for ( uint32_t t = 0; t < schema.table_count; t++ )
			if ( same_word ( schema.tables [ t ].name, qualifier, size ) )
				return &schema.tables [ t ];

		const char* text = app.editor.text.value ( );
		size_t length = app.editor.text.size;
		size_t at = 0;
		const engine::table_info* last = nullptr;

		while ( at < length ) {

			while ( at < length && !editor_word_char ( text [ at ] ) )
				at++;

			size_t start = at;

			while ( at < length && editor_word_char ( text [ at ] ) )
				at++;

			if ( at == start )
				break;

			if ( same_word ( "as", text + start, at - start ) )
				continue;

			if ( last && at - start == size && starts_folded ( text + start, qualifier, size ) )
				return last;

			last = nullptr;

			for ( uint32_t t = 0; t < schema.table_count; t++ )
				if ( same_word ( schema.tables [ t ].name, text + start, at - start ) )
					last = &schema.tables [ t ];

		}

		return nullptr;

	}

	void suggest_schema ( void* context, const char* qualifier, size_t qualifier_size, const char* prefix, size_t prefix_size, completion_list& out ) {

		studio_state& app = *static_cast < studio_state* > ( context );

		if ( !app.db || !app.db->open )
			return;

		engine::catalog& schema = *app.db->schema;

		if ( qualifier_size ) {

			const engine::table_info* table = table_for ( app, qualifier, qualifier_size );

			if ( !table )
				return;

			for ( uint32_t c = 0; c < table->column_count; c++ )
				if ( starts_folded ( table->columns [ c ].name, prefix, prefix_size ) )
					add_completion ( out, table->columns [ c ].name, base::length ( table->columns [ c ].name ), COMPLETE_COLUMN, table->columns [ c ].type );

			return;

		}

		bool lower = true;

		for ( size_t i = 0; i < prefix_size; i++ )
			if ( prefix [ i ] >= 'A' && prefix [ i ] <= 'Z' )
				lower = false;

		for ( uint32_t t = 0; t < schema.table_count; t++ )
			if ( starts_folded ( schema.tables [ t ].name, prefix, prefix_size ) )
				add_completion ( out, schema.tables [ t ].name, base::length ( schema.tables [ t ].name ), COMPLETE_TABLE, "table" );

		for ( uint32_t t = 0; t < schema.table_count; t++ )
			for ( uint32_t c = 0; c < schema.tables [ t ].column_count; c++ )
				if ( starts_folded ( schema.tables [ t ].columns [ c ].name, prefix, prefix_size ) )
					add_completion ( out, schema.tables [ t ].columns [ c ].name, base::length ( schema.tables [ t ].columns [ c ].name ), COMPLETE_COLUMN, schema.tables [ t ].name );

		for ( const char* name : suggest_functions )
			if ( prefix_size >= 2 && starts_folded ( name, prefix, prefix_size ) )
				add_completion ( out, name, base::length ( name ), COMPLETE_FUNCTION, "function" );

		for ( const char* word : suggest_keywords ) {

			if ( !starts_folded ( word, prefix, prefix_size ) )
				continue;

			char cased [ COMPLETE_WORD ];
			size_t size = base::length ( word );

			for ( size_t i = 0; i < size && i < COMPLETE_WORD - 1; i++ )
				cased [ i ] = lower ? base::lower ( word [ i ] ) : word [ i ];

			add_completion ( out, cased, size, COMPLETE_KEYWORD, "keyword" );

		}

	}

}