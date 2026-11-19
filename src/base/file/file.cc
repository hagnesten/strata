#include "base/file/file.hh"

#include <windows.h>

namespace base {

	file_faults faults;

	void plan_fault ( uint64_t after, bool torn ) {

		faults = { };
		faults.armed = true;
		faults.countdown = after;
		faults.torn = torn;

	}

	void clear_faults ( ) {

		uint64_t operations = faults.operations;

		faults = { };
		faults.operations = operations;

	}

	static bool fault_now ( ) {

		faults.operations++;

		if ( !faults.armed )
			return false;

		if ( faults.tripped )
			return true;

		if ( faults.countdown && --faults.countdown == 0 ) {

			faults.tripped = true;

			return true;

		}

		return false;

	}

	bool wide_path ( const char* path, wchar_t* out, size_t room ) {

		return MultiByteToWideChar ( CP_UTF8, 0, path, -1, out, static_cast < int32_t > ( room ) ) > 0;

	}

	bool open_file ( file_handle& out, const char* path, bool create ) {

		wchar_t wide [ FILE_PATH_ROOM ];

		out.handle = nullptr;

		if ( !wide_path ( path, wide, FILE_PATH_ROOM ) )
			return false;

		HANDLE handle = CreateFileW ( wide, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, nullptr, create ? OPEN_ALWAYS : OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_RANDOM_ACCESS, nullptr );

		if ( handle == INVALID_HANDLE_VALUE )
			return false;

		out.handle = handle;

		return true;

	}

	bool open_reading ( file_handle& out, const char* path ) {

		wchar_t wide [ FILE_PATH_ROOM ];

		out.handle = nullptr;

		if ( !wide_path ( path, wide, FILE_PATH_ROOM ) )
			return false;

		HANDLE handle = CreateFileW ( wide, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr );

		if ( handle == INVALID_HANDLE_VALUE )
			return false;

		out.handle = handle;

		return true;

	}

	static OVERLAPPED at_offset ( uint64_t offset ) {

		OVERLAPPED place = { };

		place.Offset = static_cast < DWORD > ( offset );
		place.OffsetHigh = static_cast < DWORD > ( offset >> 32 );

		return place;

	}

	bool read_at ( const file_handle& file, uint64_t offset, void* out, size_t size ) {

		uint8_t* at = static_cast < uint8_t* > ( out );

		while ( size ) {

			OVERLAPPED place = at_offset ( offset );
			DWORD chunk = static_cast < DWORD > ( min < size_t > ( size, 1u << 30 ) );
			DWORD got = 0;

			if ( !ReadFile ( file.handle, at, chunk, &got, &place ) || !got )
				return false;

			at += got;
			offset += got;
			size -= got;

		}

		return true;

	}

	bool write_at ( const file_handle& file, uint64_t offset, const void* data, size_t size ) {

		const uint8_t* at = static_cast < const uint8_t* > ( data );
		bool was_tripped = faults.tripped;

		if ( fault_now ( ) ) {

			if ( !was_tripped && faults.torn && size > 1 ) {

				OVERLAPPED place = at_offset ( offset );
				DWORD put = 0;

				WriteFile ( file.handle, at, static_cast < DWORD > ( size / 2 ), &put, &place );

			}

			return false;

		}

		while ( size ) {

			OVERLAPPED place = at_offset ( offset );
			DWORD chunk = static_cast < DWORD > ( min < size_t > ( size, 1u << 30 ) );
			DWORD put = 0;

			if ( !WriteFile ( file.handle, at, chunk, &put, &place ) || !put )
				return false;

			at += put;
			offset += put;
			size -= put;

		}

		return true;

	}

	bool sync_file ( const file_handle& file ) {

		if ( fault_now ( ) )
			return false;

		return FlushFileBuffers ( file.handle ) != 0;

	}

	uint64_t file_size ( const file_handle& file ) {

		LARGE_INTEGER size = { };

		return GetFileSizeEx ( file.handle, &size ) ? static_cast < uint64_t > ( size.QuadPart ) : 0;

	}

	bool truncate_file ( const file_handle& file, uint64_t size ) {

		if ( fault_now ( ) )
			return false;

		FILE_END_OF_FILE_INFO end = { };

		end.EndOfFile.QuadPart = static_cast < LONGLONG > ( size );

		return SetFileInformationByHandle ( file.handle, FileEndOfFileInfo, &end, sizeof ( end ) ) != 0;

	}

	void close_file ( file_handle& file ) {

		if ( file.handle )
			CloseHandle ( file.handle );

		file.handle = nullptr;

	}

	bool erase_path ( const char* path ) {

		wchar_t wide [ FILE_PATH_ROOM ];

		return wide_path ( path, wide, FILE_PATH_ROOM ) && DeleteFileW ( wide );

	}

	bool path_exists ( const char* path ) {

		wchar_t wide [ FILE_PATH_ROOM ];

		return wide_path ( path, wide, FILE_PATH_ROOM ) && GetFileAttributesW ( wide ) != INVALID_FILE_ATTRIBUTES;

	}

	bool temp_path ( char* out, size_t room, const char* name ) {

		wchar_t folder [ FILE_PATH_ROOM ];
		DWORD got = GetTempPathW ( FILE_PATH_ROOM, folder );

		if ( !got || got >= FILE_PATH_ROOM )
			return false;

		int32_t size = WideCharToMultiByte ( CP_UTF8, 0, folder, -1, out, static_cast < int32_t > ( room ), nullptr, nullptr );

		if ( size <= 0 || static_cast < size_t > ( size ) + length ( name ) >= room )
			return false;

		copy_text ( out + size - 1, room - static_cast < size_t > ( size ) + 1, name );

		return true;

	}

}
