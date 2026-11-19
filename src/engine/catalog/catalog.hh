#pragma once

#include "storage/btree/btree.hh"
#include "sql/parser/parser.hh"

#define CATALOG_TABLES 512
#define CATALOG_INDEXES 1024
#define CATALOG_KEY_COLUMNS 32
#define CATALOG_CHECKS 32
#define CATALOG_FOREIGN 32
#define SCHEMA_COLUMNS 5

namespace engine {

	struct column_info {

		const char* name;
		const char* type;
		sql::affinity_kind affinity;
		bool not_null;
		bool rowid;
		sql::expression* fallback;
		sql::expression* check;

	};

	struct foreign_key {

		uint32_t columns [ CATALOG_KEY_COLUMNS ];
		uint32_t column_count;
		const char* parent;
		const char* parent_columns [ CATALOG_KEY_COLUMNS ];
		uint32_t parent_count;
		sql::foreign_action on_delete;

	};

	struct index_info {

		const char* name;
		const char* table;
		uint32_t root;
		int64_t schema_rowid;
		bool unique;
		bool automatic;
		uint32_t columns [ CATALOG_KEY_COLUMNS ];
		uint32_t column_count;
		const char* sql;

	};

	struct table_info {

		const char* name;
		uint32_t root;
		int64_t schema_rowid;
		column_info* columns;
		uint32_t column_count;
		int32_t rowid_column;
		bool autoincrement;
		sql::expression* checks [ CATALOG_CHECKS ];
		uint32_t check_count;
		foreign_key foreign [ CATALOG_FOREIGN ];
		uint32_t foreign_count;
		sql::create_table_statement* definition;
		const char* sql;

	};

	struct catalog {

		base::arena memory;
		table_info tables [ CATALOG_TABLES ];
		uint32_t table_count;
		index_info indexes [ CATALOG_INDEXES ];
		uint32_t index_count;
		uint32_t version;

	};

	bool load_catalog ( storage::pager& pages, catalog& out, base::text& problem );

	void clear_catalog ( catalog& item );

	table_info* find_table ( catalog& item, const char* name );

	index_info* find_index ( catalog& item, const char* name );

	int32_t find_column ( const table_info& table, const char* name );

	bool describe_table ( base::arena& memory, sql::create_table_statement* definition, table_info& out, base::text& problem );

	bool write_schema_row ( storage::pager& pages, const char* kind, const char* name, const char* table, uint32_t root, const char* sql, int64_t& rowid );

	bool delete_schema_row ( storage::pager& pages, int64_t rowid );

	bool ensure_schema_tree ( storage::pager& pages );

}
