#ifndef STRATA_H
#define STRATA_H

#include <stdint.h>
#include <stddef.h>

#if defined ( _WIN32 )
#if defined ( STRATA_BUILDING )
#define STRATA_API __declspec ( dllexport )
#else
#define STRATA_API __declspec ( dllimport )
#endif
#else
#define STRATA_API __attribute__ ( ( visibility ( "default" ) ) )
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct strata_db strata_db;
typedef struct strata_stmt strata_stmt;

enum strata_code {

	STRATA_OK = 0,
	STRATA_ERROR = 1,
	STRATA_BUSY = 5,
	STRATA_NOMEM = 7,
	STRATA_READONLY = 8,
	STRATA_IOERR = 10,
	STRATA_CORRUPT = 11,
	STRATA_CONSTRAINT = 19,
	STRATA_MISMATCH = 20,
	STRATA_MISUSE = 21,
	STRATA_RANGE = 25,
	STRATA_ROW = 100,
	STRATA_DONE = 101

};

enum strata_type {

	STRATA_NULL = 0,
	STRATA_INTEGER = 1,
	STRATA_REAL = 2,
	STRATA_TEXT = 3,
	STRATA_BLOB = 4,
	STRATA_TEXT16 = 5

};

typedef struct strata_value {

	int32_t type;
	uint32_t size;

	union {

		int64_t integer;
		double real;
		const void* bytes;

	} data;

} strata_value;

STRATA_API const char* strata_version ( void );

STRATA_API int strata_open ( const char* path, strata_db** out );

STRATA_API int strata_close ( strata_db* db );

STRATA_API int strata_exec ( strata_db* db, const char* sql );

STRATA_API const char* strata_error ( strata_db* db );

STRATA_API int strata_error_line ( strata_db* db );

STRATA_API int strata_error_column ( strata_db* db );

STRATA_API int64_t strata_changes ( strata_db* db );

STRATA_API int64_t strata_total_changes ( strata_db* db );

STRATA_API int64_t strata_last_rowid ( strata_db* db );

STRATA_API int strata_in_transaction ( strata_db* db );

STRATA_API int strata_prepare ( strata_db* db, const char* sql, int size, strata_stmt** out, const char** tail );

STRATA_API int strata_finalize ( strata_stmt* stmt );

STRATA_API int strata_reset ( strata_stmt* stmt );

STRATA_API int strata_clear_bindings ( strata_stmt* stmt );

STRATA_API int strata_parameter_count ( strata_stmt* stmt );

STRATA_API int strata_parameter_index ( strata_stmt* stmt, const char* name );

STRATA_API const char* strata_parameter_name ( strata_stmt* stmt, int index );

STRATA_API int strata_bind_null ( strata_stmt* stmt, int index );

STRATA_API int strata_bind_int64 ( strata_stmt* stmt, int index, int64_t value );

STRATA_API int strata_bind_double ( strata_stmt* stmt, int index, double value );

STRATA_API int strata_bind_text ( strata_stmt* stmt, int index, const char* value, int size );

STRATA_API int strata_bind_text16 ( strata_stmt* stmt, int index, const uint16_t* value, int units );

STRATA_API int strata_bind_blob ( strata_stmt* stmt, int index, const void* value, int size );

STRATA_API int strata_bind_values ( strata_stmt* stmt, const strata_value* values, int count );

STRATA_API int strata_step ( strata_stmt* stmt );

STRATA_API int strata_column_count ( strata_stmt* stmt );

STRATA_API const char* strata_column_name ( strata_stmt* stmt, int column );

STRATA_API int strata_column_type ( strata_stmt* stmt, int column );

STRATA_API int64_t strata_column_int64 ( strata_stmt* stmt, int column );

STRATA_API double strata_column_double ( strata_stmt* stmt, int column );

STRATA_API const char* strata_column_text ( strata_stmt* stmt, int column );

STRATA_API const void* strata_column_blob ( strata_stmt* stmt, int column );

STRATA_API int strata_column_bytes ( strata_stmt* stmt, int column );

STRATA_API const strata_value* strata_row ( strata_stmt* stmt );

STRATA_API int strata_fetch ( strata_stmt* stmt, strata_value* out, int room_rows, int* rows );

STRATA_API int strata_run_batch ( strata_stmt* stmt, const strata_value* values, int rows, int width, int* done );

#ifdef __cplusplus
}
#endif

#endif