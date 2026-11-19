#pragma once

#include "engine/catalog/catalog.hh"
#include "engine/value/value.hh"

#define RUN_DEPTH 48
#define SORT_BOUND 100000
#define ENGINE_VERSION "0.5"
#define RUN_KEY_ROOM ( CATALOG_KEY_COLUMNS + 1 )

namespace engine {

	enum aggregate_kind : uint8_t {

		AGGREGATE_COUNT,
		AGGREGATE_COUNT_ALL,
		AGGREGATE_SUM,
		AGGREGATE_TOTAL,
		AGGREGATE_AVG,
		AGGREGATE_MIN,
		AGGREGATE_MAX,
		AGGREGATE_GROUP_CONCAT,
		AGGREGATE_JSON_ARRAY,
		AGGREGATE_JSON_OBJECT,

	};

	enum path_kind : uint8_t {

		PATH_SCAN,
		PATH_ROWID_EQUAL,
		PATH_ROWID_RANGE,
		PATH_ROWID_LIST,
		PATH_INDEX,
		PATH_FULLTEXT,

	};

	enum bind_mode : uint8_t {

		BIND_PLAIN,
		BIND_AGGREGATES,
		BIND_INSIDE_AGGREGATE,
		BIND_CONSTANT,

	};

	enum child_role : uint8_t {

		CHILD_SCALAR,
		CHILD_LIST,
		CHILD_EXISTS,
		CHILD_FROM,

	};

	enum function_kind : uint8_t {

		FUNCTION_ABS,
		FUNCTION_CHANGES,
		FUNCTION_CHAR,
		FUNCTION_COALESCE,
		FUNCTION_CONCAT,
		FUNCTION_CONCAT_WS,
		FUNCTION_GLOB,
		FUNCTION_HEX,
		FUNCTION_IFNULL,
		FUNCTION_IIF,
		FUNCTION_INSTR,
		FUNCTION_LAST_ROWID,
		FUNCTION_LENGTH,
		FUNCTION_LIKE,
		FUNCTION_LOWER,
		FUNCTION_LTRIM,
		FUNCTION_MAX,
		FUNCTION_MIN,
		FUNCTION_NULLIF,
		FUNCTION_OCTET_LENGTH,
		FUNCTION_PRINTF,
		FUNCTION_QUOTE,
		FUNCTION_RANDOM,
		FUNCTION_RANDOMBLOB,
		FUNCTION_REPLACE,
		FUNCTION_ROUND,
		FUNCTION_RTRIM,
		FUNCTION_SIGN,
		FUNCTION_SUBSTR,
		FUNCTION_TOTAL_CHANGES,
		FUNCTION_TRIM,
		FUNCTION_TYPEOF,
		FUNCTION_UNICODE,
		FUNCTION_UPPER,
		FUNCTION_VERSION,
		FUNCTION_ZEROBLOB,
		FUNCTION_DATE,
		FUNCTION_TIME,
		FUNCTION_DATETIME,
		FUNCTION_JULIANDAY,
		FUNCTION_UNIXEPOCH,
		FUNCTION_STRFTIME,
		FUNCTION_JSON,
		FUNCTION_JSON_VALID,
		FUNCTION_JSON_EXTRACT,
		FUNCTION_JSON_TYPE,
		FUNCTION_JSON_ARRAY,
		FUNCTION_JSON_OBJECT,
		FUNCTION_JSON_ARRAY_LENGTH,
		FUNCTION_JSON_QUOTE,
		FUNCTION_BM25,
		FUNCTION_HIGHLIGHT,
		FUNCTION_SNIPPET,
		FUNCTION_COUNT,
		FUNCTION_SUM,
		FUNCTION_TOTAL,
		FUNCTION_AVG,
		FUNCTION_MIN_AGGREGATE,
		FUNCTION_MAX_AGGREGATE,
		FUNCTION_GROUP_CONCAT,
		FUNCTION_JSON_GROUP_ARRAY,
		FUNCTION_JSON_GROUP_OBJECT,
		FUNCTION_ROW_NUMBER,
		FUNCTION_RANK,
		FUNCTION_DENSE_RANK,
		FUNCTION_PERCENT_RANK,
		FUNCTION_CUME_DIST,
		FUNCTION_NTILE,
		FUNCTION_LAG,
		FUNCTION_LEAD,
		FUNCTION_FIRST_VALUE,
		FUNCTION_LAST_VALUE,
		FUNCTION_NTH_VALUE,

	};

	struct function_entry {

		const char* name;
		uint8_t low;
		uint8_t high;
		bool aggregate;
		function_kind kind;

	};

	struct prepared_select;

	struct value_set_entry {

		uint32_t hash;
		storage::value* values;
		value_set_entry* next;

	};

	struct value_set {

		value_set_entry** buckets;
		uint32_t bucket_count;
		uint32_t count;
		uint32_t width;
		bool has_null;

	};

	struct access_path {

		path_kind kind;
		index_info* index;
		uint32_t equal_count;
		sql::expression* equal [ CATALOG_KEY_COLUMNS ];
		sql::expression* low;
		bool low_inclusive;
		sql::expression* high;
		bool high_inclusive;
		sql::expression* list;
		bool covering;

	};

	struct cte_entry {

		const sql::common_table* syntax;
		prepared_select* plan;
		const char** names;
		uint32_t width;
		bool preparing;
		bool recursive;
		bool distinct;
		storage::value* working;
		uint32_t working_count;

	};

	struct cte_scope {

		cte_entry* entries;
		uint32_t count;
		cte_scope* next;

	};

	struct prepared_source {

		const sql::table_source* syntax;
		table_info* table;
		prepared_select* query;
		cte_entry* cte;
		bool working;
		const char* alias;
		const char** names;
		sql::affinity_kind* affinities;
		bool* hidden;
		bool* nocase;
		bool* used;
		uint32_t width;
		uint32_t position;
		bool left;
		bool right;
		int32_t* paired_source;
		int32_t* paired_column;
		sql::expression** on_terms;
		uint32_t on_count;
		sql::expression** filters;
		uint32_t filter_count;
		uint32_t filter_room;
		access_path path;

	};

	struct plan_child {

		child_role role;
		prepared_select* plan;
		plan_child* next;

	};

	struct prepared_select {

		const sql::select_statement* syntax;
		prepared_select* parent;
		prepared_source* sources;
		uint32_t source_count;
		sql::expression** outputs;
		const char** names;
		bool* aliased;
		uint32_t output_count;
		sql::expression** aggregates;
		uint32_t aggregate_count;
		uint32_t aggregate_room;
		sql::expression** windows;
		uint32_t window_count;
		uint32_t window_room;
		uint32_t* window_offsets;
		uint32_t window_width;
		sql::expression** group;
		uint32_t group_count;
		sql::expression* having;
		sql::expression** constant_filters;
		uint32_t constant_count;
		uint32_t constant_room;
		sql::expression** order_values;
		int32_t* order_output;
		bool* order_desc;
		bool* order_nocase;
		uint32_t order_count;
		sql::expression* limit;
		sql::expression* offset;
		prepared_select* next;
		sql::compound_kind compound;
		bool grouping;
		bool correlated;
		bool distinct;
		bool values_only;
		bool uses_working;
		bool order_by_index;
		int32_t snapshot_aggregate;
		plan_child* children;
		uint32_t id;

	};

	struct source_state {

		prepared_source* plan;
		storage::btree tree;
		storage::btree index_tree;
		storage::cursor at;
		storage::cursor index_at;
		storage::value* values;
		int64_t rowid;
		bool null_row;
		bool done;
		bool first;
		int64_t low;
		int64_t high;
		const uint8_t* equal_key;
		size_t equal_size;
		const uint8_t* high_key;
		size_t high_size;
		uint32_t high_columns;
		bool high_inclusive;
		const uint8_t* low_key;
		size_t low_size;
		bool low_exclusive;
		int64_t* list;
		uint32_t list_count;
		uint32_t list_at;
		storage::value* rows;
		uint32_t row_count;
		uint32_t row_at;
		bool materialized;
		value_set matched;

	};

	struct query_state {

		prepared_select* plan;
		source_state* sources;
		query_state* parent;
		storage::value* aggregate_values;
		storage::value* window_values;
		struct window_rows* captured;

	};

	struct window_row {

		int64_t* rowids;
		storage::value** rows;
		bool* null_rows;
		storage::value* aggregates;
		storage::value* inputs;
		uint32_t index;

	};

	struct window_rows {

		window_row** rows;
		uint32_t count;
		uint32_t room;

	};

	struct window_order {

		uint32_t offset;
		uint32_t partitions;
		uint32_t orders;
		const sql::window_spec* spec;

	};

	struct table_scope {

		table_info* table;
		prepared_select* plan;
		query_state state;
		uint32_t* generated_order;
		uint32_t generated_count;
		table_scope* next;

	};

	struct upsert_plan {

		const sql::insert_statement* syntax;
		prepared_select* scope;
		int32_t target;
		bool any_target;
		int32_t* columns;

	};

	struct insert_plan {

		table_info* table;
		int32_t* targets;
		uint32_t target_count;
		bool* targeted;
		prepared_select* query;
		upsert_plan upsert;
		bool upserting;

	};

	struct target_plan {

		table_info* table;
		prepared_select* plan;
		int32_t* columns;

	};

	struct parent_link {

		table_info* parent;
		bool by_rowid;
		index_info* index;
		uint32_t columns [ CATALOG_KEY_COLUMNS ];
		uint32_t count;

	};

	struct child_hit {

		table_info* table;
		const foreign_key* key;
		int64_t rowid;

	};

	struct cached_child {

		const sql::expression* key;
		storage::value scalar;
		bool exists;
		value_set set;
		cached_child* next;

	};

	struct run_context {

		storage::pager* pages;
		catalog* schema;
		base::arena* lasting;
		base::arena* memory;
		base::arena* scratch;
		base::arena* results;
		const storage::value* parameters;
		uint32_t parameter_count;
		base::text* problem;
		base::text row_bytes;
		uint64_t rows_examined;
		int64_t changes;
		int64_t last_rowid;
		int64_t previous_changes;
		int64_t total_changes;
		uint64_t* random;
		cached_child* cache;
		table_scope* scopes;
		cte_scope* ctes;
		cte_entry* pending_cte;
		struct search_entry* searches;
		uint32_t depth;
		uint32_t next_id;
		bool schema_changed;
		bool mark_pending;
		bool marked;
		bool unmarked_writes;

	};

	struct row_sink {

		void* context;
		bool ( *emit ) ( void* context, const storage::value* values, uint32_t count );

	};

	struct sorted_row {

		storage::value* values;
		storage::value* keys;
		uint64_t sequence;

	};

	struct output_pipe {

		uint32_t width;
		uint32_t key_count;
		const bool* descending;
		const bool* nocase;
		sql::expression* const* outputs;
		bool distinct;
		bool sorting;
		value_set unique;
		sorted_row** rows;
		uint32_t row_count;
		uint32_t row_room;
		int64_t skip;
		int64_t remaining;
		int64_t bound;
		uint64_t arrivals;
		row_sink* sink;
		bool stopped;

	};

	struct collected_rows {

		storage::value* values;
		uint32_t count;
		uint32_t room;
		uint32_t width;
		base::arena* memory;

	};

	struct aggregate_state;

	struct group_state {

		uint32_t hash;
		storage::value* keys;
		aggregate_state* states;
		int64_t* rowids;
		storage::value** rows;
		bool* null_rows;
		bool has_snapshot;
		group_state* chain;
		group_state* next;

	};

	struct group_table {

		group_state** buckets;
		uint32_t bucket_count;
		uint32_t count;
		group_state* first;
		group_state* last;

	};

	struct first_row_sink {

		base::text bytes;
		storage::value value;
		uint32_t width;
		bool seen;

	};

	struct member_sink {

		run_context* ctx;
		storage::value probe;
		sql::affinity_kind affinity;
		bool found;
		bool has_null;
		bool any;

	};

	struct set_sink {

		base::arena* memory;
		value_set* set;
		bool failed;
		sql::affinity_kind left;
		sql::affinity_kind right;

	};

	struct aggregate_state {

		aggregate_kind kind;
		uint64_t count;
		int64_t sum_integer;
		double sum_real;
		bool real_seen;
		bool overflow;
		bool has_best;
		bool improved;
		storage::value best;
		char* joined;
		uint32_t joined_size;
		uint32_t joined_room;
		bool distinct;
		value_set seen;

	};

	bool fail_run ( run_context& ctx, const char* message );

	bool fail_named ( run_context& ctx, const char* first, const char* name, const char* rest );

	bool fail_storage ( run_context& ctx, const char* what );

	bool begin_writes ( run_context& ctx, bool risky );

	prepared_select* prepare_select ( run_context& ctx, sql::select_statement* item, prepared_select* parent );

	prepared_select* prepare_table_scope ( run_context& ctx, table_info* table, const char* alias, table_info* second, const char* second_alias );

	bool bind_expression ( run_context& ctx, prepared_select* scope, sql::expression* item, bind_mode mode );

	bool split_terms ( run_context& ctx, sql::expression* item, sql::expression**& out, uint32_t& count, uint32_t& room );

	int32_t highest_source ( const sql::expression* item, uint32_t nest );

	sql::affinity_kind expression_affinity ( const sql::expression* item );

	bool evaluate ( run_context& ctx, query_state* scope, const sql::expression* item, storage::value& out );

	bool condition ( run_context& ctx, query_state* scope, const sql::expression* item, truth_kind& out );

	bool passes ( run_context& ctx, query_state* scope, sql::expression* const* terms, uint32_t count, bool& out );

	void comparison_affinity ( base::arena& memory, sql::affinity_kind left, sql::affinity_kind right, storage::value& a, storage::value& b );

	storage::value seek_value ( base::arena& memory, sql::affinity_kind affinity, const storage::value& item );

	int32_t function_id ( const char* name, uint32_t args, bool star, bool& aggregate );

	bool function_named ( const char* name );

	bool call_function ( run_context& ctx, const sql::expression* item, const storage::value* args, uint32_t count, storage::value& out );

	void start_aggregate ( aggregate_state& state, const sql::expression* item );

	bool step_aggregate ( run_context& ctx, aggregate_state& state, const storage::value* args, uint32_t count );

	bool finish_aggregate ( run_context& ctx, aggregate_state& state, storage::value& out );

	bool set_add ( base::arena& memory, value_set& set, const storage::value* values, uint32_t width, bool& added );

	bool set_has ( const value_set& set, const storage::value* values, uint32_t width );

	uint32_t hash_row ( const storage::value* values, uint32_t width );

	bool same_row ( const storage::value* a, const storage::value* b, uint32_t width );

	storage::value* copy_row ( base::arena& memory, const storage::value* values, uint32_t width );

	bool run_plan ( run_context& ctx, prepared_select* plan, query_state* parent, row_sink& sink );

	bool collect_plan ( run_context& ctx, prepared_select* plan, query_state* parent, collected_rows& out );

	bool collect_recursive ( run_context& ctx, cte_entry& cte, query_state* parent, collected_rows& out );

	bool capture_window_row ( run_context& ctx, query_state& scope, window_rows& out );

	bool compute_windows ( run_context& ctx, query_state& scope, window_rows& rows, storage::value*& out );

	bool restore_window_row ( run_context& ctx, query_state& scope, const window_row& row );

	bool window_function ( int32_t id );

	bool call_date_function ( run_context& ctx, function_kind kind, const storage::value* args, uint32_t count, storage::value& out );

	bool call_json_function ( run_context& ctx, function_kind kind, const storage::value* args, uint32_t count, storage::value& out );

	bool json_arrow ( run_context& ctx, const storage::value& document, const storage::value& path, bool value, storage::value& out );

	bool append_json ( run_context& ctx, base::text& out, const storage::value& item );

	bool run_child ( run_context& ctx, query_state* scope, const sql::expression* item, storage::value& out );

	bool open_source ( run_context& ctx, query_state* scope, source_state& source );

	bool next_source ( run_context& ctx, query_state* scope, source_state& source, bool& found );

	bool decode_row ( run_context& ctx, const table_info& table, const uint8_t* payload, size_t size, int64_t rowid, storage::value* values );

	table_scope* scope_for ( run_context& ctx, table_info& table );

	bool compute_generated ( run_context& ctx, const table_info& table, storage::value* values, int64_t rowid, uint32_t from );

	bool read_row ( run_context& ctx, const table_info& table, int64_t rowid, storage::value*& values, bool& found );

	bool make_index_key ( run_context& ctx, const index_info& index, const storage::value* values, int64_t rowid, bool with_rowid, base::text& out );

	void choose_path ( run_context& ctx, prepared_select* plan, uint32_t level );

	void describe_path ( const prepared_source& source, base::text& out );

	bool fallback_value ( run_context& ctx, const column_info& column, storage::value& out );

	bool run_insert ( run_context& ctx, const sql::insert_statement* item );

	bool run_update ( run_context& ctx, const sql::update_statement* item );

	bool run_delete ( run_context& ctx, const sql::delete_statement* item );

	bool plan_target ( run_context& ctx, const char* name, sql::expression* where, prepared_select*& plan );

	bool insert_values ( run_context& ctx, table_info& table, storage::value* values, const storage::value* explicit_rowid, sql::conflict_kind conflict, upsert_plan* upsert, bool& written );

	bool run_create_table ( run_context& ctx, const sql::create_table_statement* item );

	bool run_create_index ( run_context& ctx, const sql::create_index_statement* item );

	bool run_drop ( run_context& ctx, const sql::drop_statement* item, bool index );

	bool run_alter ( run_context& ctx, const sql::alter_statement* item );

	bool run_analyze ( run_context& ctx );

	void order_joins ( run_context& ctx, prepared_select* plan );

	bool explain_plan ( run_context& ctx, prepared_select* plan, row_sink& sink );

}
