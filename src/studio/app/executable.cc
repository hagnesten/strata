#include "studio/app/studio.hh"
#include "base/file/file.hh"

#include <windows.h>

namespace studio {

	bool make_studio_copy ( ) {

		wchar_t own [ MAX_PATH ];
		DWORD size = GetModuleFileNameW ( nullptr, own, MAX_PATH );

		if ( !size || size >= MAX_PATH - 16 )
			return false;

		char path [ FILE_PATH_ROOM ];

		if ( WideCharToMultiByte ( CP_UTF8, 0, own, -1, path, sizeof ( path ), nullptr, nullptr ) <= 0 )
			return false;

		base::file_handle file;

		if ( !base::open_reading ( file, path ) )
			return false;

		uint64_t bytes = base::file_size ( file );
		uint8_t* image = base::make < uint8_t > ( static_cast < size_t > ( bytes ) );
		bool fine = image && base::read_at ( file, 0, image, static_cast < size_t > ( bytes ) );

		base::close_file ( file );

		if ( fine ) {

			uint32_t header = *reinterpret_cast < uint32_t* > ( image + 0x3C );
			uint64_t field = static_cast < uint64_t > ( header ) + 4 + 20 + 68;

			fine = field + 2 <= bytes && image [ header ] == 'P' && image [ header + 1 ] == 'E';

			if ( fine ) {

				image [ field ] = IMAGE_SUBSYSTEM_WINDOWS_GUI;
				image [ field + 1 ] = 0;

				base::text target;
				size_t cut = base::length ( path );

				while ( cut && path [ cut - 1 ] != '\\' )
					cut--;

				target.add ( path, cut ).add ( "strata-studio.exe" );

				base::file_handle out;

				fine = base::open_file ( out, target.value ( ), true );

				if ( fine ) {

					fine = base::truncate_file ( out, 0 ) && base::write_at ( out, 0, image, static_cast < size_t > ( bytes ) );
					base::close_file ( out );

				}

			}

		}

		base::release ( image );

		return fine;

	}

}