#include "base/clipboard/clipboard.hh"

#include <windows.h>

namespace base {

	bool copy_to_clipboard ( const char* value, size_t size ) {

		int32_t count = size ? MultiByteToWideChar ( CP_UTF8, 0, value, static_cast < int > ( size ), nullptr, 0 ) : 0;
		HGLOBAL block = GlobalAlloc ( GMEM_MOVEABLE, ( static_cast < size_t > ( count ) + 1 ) * sizeof ( wchar_t ) );

		if ( !block )
			return false;

		wchar_t* wide = static_cast < wchar_t* > ( GlobalLock ( block ) );

		if ( !wide ) {

			GlobalFree ( block );

			return false;

		}

		if ( count )
			MultiByteToWideChar ( CP_UTF8, 0, value, static_cast < int > ( size ), wide, count );

		wide [ count ] = 0;
		GlobalUnlock ( block );

		if ( !OpenClipboard ( nullptr ) ) {

			GlobalFree ( block );

			return false;

		}

		EmptyClipboard ( );

		bool fine = SetClipboardData ( CF_UNICODETEXT, block ) != nullptr;

		CloseClipboard ( );

		if ( !fine )
			GlobalFree ( block );

		return fine;

	}

	bool paste_from_clipboard ( text& out ) {

		out.clear ( );

		if ( !OpenClipboard ( nullptr ) )
			return false;

		HANDLE block = GetClipboardData ( CF_UNICODETEXT );
		bool fine = false;

		if ( block ) {

			const wchar_t* wide = static_cast < const wchar_t* > ( GlobalLock ( block ) );

			if ( wide ) {

				int32_t size = WideCharToMultiByte ( CP_UTF8, 0, wide, -1, nullptr, 0, nullptr, nullptr );

				if ( size > 1 && out.reserve ( static_cast < size_t > ( size ) ) ) {

					WideCharToMultiByte ( CP_UTF8, 0, wide, -1, out.data, size, nullptr, nullptr );
					out.size = static_cast < size_t > ( size - 1 );
					fine = true;

				}

				GlobalUnlock ( block );

			}

		}

		CloseClipboard ( );

		return fine;

	}

}
