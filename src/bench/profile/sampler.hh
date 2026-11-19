#pragma once

#include <stdint.h>

#define SAMPLER_DEPTH 16
#define SAMPLER_ROOM 400000
#define SAMPLER_MICROSECONDS 200

namespace bench {

	struct sampler {

		void* target;
		void* worker;
		volatile bool stopping;
		volatile bool paused;
		uint64_t* frames;
		uint8_t* depths;
		uint32_t count;
		uint32_t lost;

	};

	bool start_sampler ( sampler& item );

	bool stop_sampler ( sampler& item, const char* path );

}
