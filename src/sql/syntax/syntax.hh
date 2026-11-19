#pragma once

#include "base/arena/arena.hh"
#include "storage/record/record.hh"

namespace sql {

	enum expression_kind : uint8_t {

		EXPRESSION_LITERAL,
		EXPRESSION_PARAMETER,
		EXPRESSION_COLUMN,
		EXPRESSION_UNARY,
		EXPRESSION_BINARY,
		EXPRESSION_FUNCTION,
		EXPRESSION_IN,
		EXPRESSION_BETWEEN,
		EXPRESSION_NULL_TEST,
		EXPRESSION_LIKE,
		EXPRESSION_CASE,
		EXPRESSION_CAST,
		EXPRESSION_SUBQUERY,
		EXPRESSION_EXISTS,

	};

	enum operator_kind : uint8_t {

		OPERATOR_NONE,
		OPERATOR_ADD,
		OPERATOR_SUBTRACT,
		OPERATOR_MULTIPLY,
		OPERATOR_DIVIDE,
		OPERATOR_MODULO,
		OPERATOR_CONCAT,
		OPERATOR_EQUAL,
		OPERATOR_NOT_EQUAL,
		OPERATOR_LESS,
		OPERATOR_LESS_EQUAL,
		OPERATOR_GREATER,
		OPERATOR_GREATER_EQUAL,
		OPERATOR_IS,
		OPERATOR_IS_NOT,
		OPERATOR_AND,
		OPERATOR_OR,
		OPERATOR_NOT,
		OPERATOR_NEGATE,
		OPERATOR_PLUS,
		OPERATOR_BIT_AND,
		OPERATOR_BIT_OR,
		OPERATOR_BIT_NOT,
		OPERATOR_SHIFT_LEFT,
		OPERATOR_SHIFT_RIGHT,

	};

	enum statement_kind : uint8_t {

		STATEMENT_SELECT,
		STATEMENT_INSERT,
		STATEMENT_UPDATE,
		STATEMENT_DELETE,
		STATEMENT_CREATE_TABLE,
		STATEMENT_CREATE_INDEX,
		STATEMENT_DROP_TABLE,
		STATEMENT_DROP_INDEX,
		STATEMENT_ALTER_TABLE,
		STATEMENT_BEGIN,
		STATEMENT_COMMIT,
		STATEMENT_ROLLBACK,
		STATEMENT_EXPLAIN,
		STATEMENT_ANALYZE,
		STATEMENT_VACUUM,

	};

	enum join_kind : uint8_t {

		JOIN_FIRST,
		JOIN_INNER,
		JOIN_LEFT,
		JOIN_CROSS,

	};

	enum compound_kind : uint8_t {

		COMPOUND_NONE,
		COMPOUND_UNION,
		COMPOUND_UNION_ALL,
		COMPOUND_INTERSECT,
		COMPOUND_EXCEPT,

	};

	enum affinity_kind : uint8_t {

		AFFINITY_ANY,
		AFFINITY_INTEGER,
		AFFINITY_REAL,
		AFFINITY_NUMERIC,
		AFFINITY_TEXT,
		AFFINITY_BLOB,

	};

	enum conflict_kind : uint8_t {

		CONFLICT_ABORT,
		CONFLICT_REPLACE,
		CONFLICT_IGNORE,
		CONFLICT_NOTHING,
		CONFLICT_UPDATE,

	};

	enum alter_kind : uint8_t {

		ALTER_ADD_COLUMN,
		ALTER_RENAME_TABLE,
		ALTER_RENAME_COLUMN,

	};

	enum foreign_action : uint8_t {

		FOREIGN_RESTRICT,
		FOREIGN_CASCADE,
		FOREIGN_SET_NULL,

	};

	enum frame_kind : uint8_t {

		FRAME_DEFAULT,
		FRAME_ROWS,
		FRAME_RANGE,

	};

	enum bound_kind : uint8_t {

		BOUND_UNBOUNDED_PRECEDING,
		BOUND_PRECEDING,
		BOUND_CURRENT,
		BOUND_FOLLOWING,
		BOUND_UNBOUNDED_FOLLOWING,

	};

	struct select_statement;

	struct expression;

	struct order_term;

	struct window_spec {

		expression** partition;
		uint32_t partition_count;
		order_term* order;
		uint32_t order_count;
		frame_kind frame;
		bound_kind start;
		expression* start_offset;
		bound_kind end;
		expression* end_offset;

	};

	struct common_table {

		const char* name;
		const char** columns;
		uint32_t column_count;
		select_statement* query;

	};

	struct expression {

		expression_kind kind;
		operator_kind op;
		bool negated;
		bool distinct;
		bool star;
		affinity_kind cast;
		storage::value literal;
		uint32_t parameter;
		const char* table;
		const char* name;
		expression* left;
		expression* right;
		expression* third;
		expression** list;
		uint32_t count;
		select_statement* query;
		int32_t source;
		int32_t column;
		uint32_t depth;
		int32_t aggregate;
		uint32_t offset;
		window_spec* window;
		int32_t window_index;

	};

	struct result_column {

		expression* value;
		const char* alias;
		bool star;
		const char* star_table;
		const char* written;

	};

	struct table_source {

		const char* name;
		const char* alias;
		select_statement* query;
		join_kind join;
		expression* on;
		const char** using_names;
		uint32_t using_count;

	};

	struct order_term {

		expression* value;
		bool descending;

	};

	struct select_statement {

		bool distinct;
		result_column* columns;
		uint32_t column_count;
		table_source* sources;
		uint32_t source_count;
		expression* where;
		expression** group;
		uint32_t group_count;
		expression* having;
		order_term* order;
		uint32_t order_count;
		expression* limit;
		expression* offset;
		compound_kind compound;
		select_statement* next;
		expression** values;
		uint32_t value_rows;
		uint32_t value_columns;
		common_table* with;
		uint32_t with_count;
		bool recursive;
		void* prepared;

	};

	struct column_definition {

		const char* name;
		const char* type;
		affinity_kind affinity;
		bool not_null;
		bool primary;
		bool descending;
		bool unique;
		bool autoincrement;
		expression* fallback;
		const char* fallback_text;
		expression* check;
		const char* references;
		const char* references_column;
		foreign_action on_delete;

	};

	struct table_constraint {

		bool primary;
		bool unique;
		const char** columns;
		uint32_t column_count;
		expression* check;
		const char* references;
		const char** references_columns;
		uint32_t references_count;
		foreign_action on_delete;

	};

	struct create_table_statement {

		const char* name;
		bool if_not_exists;
		column_definition* columns;
		uint32_t column_count;
		table_constraint* constraints;
		uint32_t constraint_count;
		select_statement* query;

	};

	struct create_index_statement {

		const char* name;
		const char* table;
		bool unique;
		bool if_not_exists;
		const char** columns;
		uint32_t column_count;

	};

	struct assignment {

		const char* column;
		expression* value;

	};

	struct insert_statement {

		const char* table;
		const char** columns;
		uint32_t column_count;
		select_statement* query;
		conflict_kind conflict;
		const char** conflict_columns;
		uint32_t conflict_count;
		assignment* updates;
		uint32_t update_count;
		expression* update_where;

	};

	struct update_statement {

		const char* table;
		assignment* assignments;
		uint32_t assignment_count;
		expression* where;
		conflict_kind conflict;

	};

	struct delete_statement {

		const char* table;
		expression* where;

	};

	struct drop_statement {

		const char* name;
		bool if_exists;

	};

	struct alter_statement {

		const char* table;
		alter_kind kind;
		const char* old_name;
		const char* new_name;
		column_definition* column;

	};

	struct statement {

		statement_kind kind;
		select_statement* select;
		insert_statement* insert;
		update_statement* update;
		delete_statement* remove;
		create_table_statement* create_table;
		create_index_statement* create_index;
		drop_statement* drop;
		alter_statement* alter;
		statement* inner;
		bool query_plan;
		const char* text;
		uint32_t text_size;
		uint32_t parameter_count;

	};

	struct parse_error {

		bool failed;
		uint32_t line;
		uint32_t column;
		base::text message;

	};

	affinity_kind affinity_of ( const char* type );

	const char* operator_text ( operator_kind op );

}
