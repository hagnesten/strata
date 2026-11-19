#pragma once

#include "studio/widgets/field.hh"
#include "base/file/folder.hh"

#define STUDIO_RECENT 10
#define RECENT_FILE "recent.txt"
#define HISTORY_FILE "history.txt"
#define SAVED_FILE "saved.txt"

namespace studio {

	struct studio_state;

	struct recent_files {

		char paths [ STUDIO_RECENT ] [ FILE_PATH_ROOM ];
		uint32_t count;

	};

	struct picker_state {

		text_field folder;
		text_field name;
		base::folder_list entries;
		bool listed;
		bool drives;
		float scroll;
		base::text problem;

	};

	bool settings_file ( base::text& out, const char* name );

	bool documents_folder ( base::text& out );

	void load_settings ( studio_state& app );

	void remember_file ( studio_state& app, const char* path );

	void open_picker ( studio_state& app );

	void draw_open_overlay ( studio_state& app, const ui::rect& area );

	void parent_folder ( const char* path, base::text& out );

	void file_name_of ( const char* path, base::text& out );

}