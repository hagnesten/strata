#pragma once

#include "studio/widgets/field.hh"
#include "engine/database/database.hh"

#define IMPORT_PREVIEW 6
#define IMPORT_COLUMNS 256

namespace studio {

	struct studio_state;

	enum transfer_format : uint8_t {

		FORMAT_CSV,
		FORMAT_JSON,

	};

	enum transfer_mode : uint8_t {

		TRANSFER_EXPORT,
		TRANSFER_IMPORT,

	};

	struct import_table {

		base::arena memory;
		const char** names;
		sql::affinity_kind* types;
		uint32_t column_count;
		const char** cells;
		uint32_t cell_count;
		uint32_t cell_room;
		uint32_t rows;

	};

	struct export_state {

		base::arena memory;
		base::text out;
		const char** names;
		uint32_t name_count;
		transfer_format format;
		uint64_t rows;

	};

	struct transfer_state {

		transfer_mode mode;
		transfer_format format;
		text_field path;
		text_field table;
		base::text source;
		base::text sql;
		import_table preview;
		bool parsed;
		bool from_clipboard;
		base::text problem;

	};

	bool parse_csv ( const char* text, size_t size, import_table& out, base::text& problem );

	bool parse_json_rows ( const char* text, size_t size, import_table& out, base::text& problem );

	void infer_types ( import_table& table );

	void write_csv_header ( base::text& out, const char* const* names, uint32_t count );

	void write_csv_row ( base::text& out, const storage::value* values, uint32_t count );

	void write_json_row ( base::text& out, const char* const* names, const storage::value* values, uint32_t count, bool first );

	bool export_rows ( engine::database& db, const char* sql, transfer_format format, const char* path, uint64_t& rows, base::text& problem );

	bool import_rows ( engine::database& db, const char* name, const import_table& table, base::text& problem );

	void open_export ( studio_state& app, const char* sql, const char* name );

	void open_import ( studio_state& app );

	void draw_transfer_overlay ( studio_state& app, const ui::rect& area );

}