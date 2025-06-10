#ifndef __LOG_ANALYSIS_H__
#define __LOG_ANALYSIS_H__
#include <stdbool.h>

enum output_types {
	OUTPUT_NONE,
	OUTPUT_CSV,
	OUTPUT_JSON,
	OUTPUT_SUMMARY,
	OUTPUT_MP4,
	OUTPUT_MAX
};

struct log_analysis_opt {
	char *file_name_in;
	char *file_name_out;
	enum output_types out_type;
	bool local_play;
};

#endif /* __LOG_ANALYSIS_H__ */
