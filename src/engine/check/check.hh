#pragma once

#include "engine/database/database.hh"

#define CHECK_PROBLEMS 200

namespace engine {

	struct check_report {

		base::arena memory;
		const char** problems;
		uint32_t problem_count;
		uint32_t problem_room;
		uint8_t* bad;
		uint32_t pages;
		uint32_t trees;
		uint32_t free_pages;
		uint64_t rows;
		uint64_t entries;
		bool done;

	};

	bool check_database ( database& db, check_report& out );

	void forget_report ( check_report& out );

}