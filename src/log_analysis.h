#ifndef __LOG_ANALYSIS_H__
#define __LOG_ANALYSIS_H__
enum output_types {
	OUTPUT_NONE,
	OUTPUT_CSV,
	OUTPUT_JSON,
	OUTPUT_MAX
};

struct log_analysis_opt {
	char *file_name_in;
	char *file_name_out;
	enum output_types out_type;
};

#endif /* __LOG_ANALYSIS_H__ */
