#pragma once

#include "storage/btree/btree.hh"
#include "sql/parser/parser.hh"

#define CATALOG_TABLES 512
#define CATALOG_INDEXES 1024
#define CATALOG_KEY_COLUMNS 32
#define CATALOG_CHECKS 32
#define CATALOG_FOREIGN 32
#define SCHEMA_COLUMNS 5
#define STAT_TABLE "strata_stat"
#define STAT_COLUMNS 3
#define SEQUENCE_TABLE "strata_sequence"
#define SEQUENCE_COLUMNS 2
#define CATALOG_SEARCH_ROOM 64
#define CATALOG_NAME_ROOM 256
#define CATALOG_VIEWS 256
#define CATALOG_SEQUENCES 256

namespace engine {

	struct column_info {

		const char* name;
		const char* type;
		sql::affinity_kind affinity;
		bool not_null;
		bool rowid;
		bool nocase;
		bool exact;
		bool binary_json;
		bool identity;
		uint8_t moment;
		int32_t precision;
		int32_t scale;
		sql::expression* fallback;
		sql::expression* check;
		sql::expression* generated;

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
		uint32_t table_root;
		int64_t schema_rowid;
		bool unique;
		bool fulltext;
		bool automatic;
		uint32_t columns [ CATALOG_KEY_COLUMNS ];
		uint32_t column_count;
		const char* sql;
		uint64_t stat_average [ CATALOG_KEY_COLUMNS ];
		bool stat_known;

	};

	struct table_info {

		const char* name;
		uint32_t root;
		int64_t schema_rowid;
		column_info* columns;
		uint32_t column_count;
		int32_t rowid_column;
		uint32_t primary [ CATALOG_KEY_COLUMNS ];
		uint32_t primary_count;
		bool autoincrement;
		sql::expression* checks [ CATALOG_CHECKS ];
		uint32_t check_count;
		uint32_t generated_count;
		uint32_t identity_count;
		foreign_key foreign [ CATALOG_FOREIGN ];
		uint32_t foreign_count;
		sql::create_table_statement* definition;
		const char* sql;
		uint64_t stat_rows;
		bool stat_known;

	};

	struct view_info {

		const char* name;
		const char* sql;
		int64_t schema_rowid;

	};

	struct sequence_info {

		const char* name;
		const char* sql;
		int64_t schema_rowid;
		int64_t start;
		int64_t increment;
		int64_t minimum;
		int64_t maximum;
		bool cycle;

	};

	struct catalog {

		base::arena memory;
		table_info tables [ CATALOG_TABLES ];
		uint32_t table_count;
		index_info indexes [ CATALOG_INDEXES ];
		uint32_t index_count;
		view_info views [ CATALOG_VIEWS ];
		uint32_t view_count;
		sequence_info sequences [ CATALOG_SEQUENCES ];
		uint32_t sequence_count;
		uint32_t version;
		uint64_t loads;
		char search [ CATALOG_SEARCH_ROOM ];
		char session_user [ CATALOG_SEARCH_ROOM ];
		char session_database [ CATALOG_SEARCH_ROOM ];
		uint32_t session_id;
		bool postgres;

	};

	inline bool owned_by ( const index_info& index, const table_info& table ) {

		return index.table_root ? index.table_root == table.root : base::same_folded ( index.table, table.name );

	}

	bool load_catalog ( storage::pager& pages, catalog& out, base::text& problem );

	void clear_catalog ( catalog& item );

	table_info* find_table ( catalog& item, const char* name );

	bool search_name ( const catalog& item, const char* name, char* out, size_t room );

	bool set_search ( catalog& item, const char* schema );

	void setting_value ( const catalog& item, const char* name, base::text& out );

	index_info* find_index ( catalog& item, const char* name );

	view_info* find_view ( catalog& item, const char* name );

	sequence_info* find_sequence ( catalog& item, const char* name );

	void sequence_limits ( const sql::sequence_statement& item, sequence_info& out );

	int32_t find_column ( const table_info& table, const char* name );

	bool describe_table ( base::arena& memory, sql::create_table_statement* definition, table_info& out, base::text& problem );

	bool write_schema_row ( storage::pager& pages, const char* kind, const char* name, const char* table, uint32_t root, const char* sql, int64_t& rowid );

	bool delete_schema_row ( storage::pager& pages, int64_t rowid );

	bool ensure_schema_tree ( storage::pager& pages );

	bool load_statistics ( storage::pager& pages, catalog& out );

	uint64_t table_rows ( storage::pager& pages, const table_info& table );

}
