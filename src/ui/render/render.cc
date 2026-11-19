#include "ui/render/render.hh"
#include "base/png/png.hh"
#include "base/file/file.hh"

namespace ui {

	color rgb ( uint32_t value, float alpha ) {

		return { static_cast < float > ( value >> 16 & 0xFF ) / 255.0f, static_cast < float > ( value >> 8 & 0xFF ) / 255.0f, static_cast < float > ( value & 0xFF ) / 255.0f, alpha };

	}

	color mix ( color a, color b, float amount ) {

		return { a.r + ( b.r - a.r ) * amount, a.g + ( b.g - a.g ) * amount, a.b + ( b.b - a.b ) * amount, a.a + ( b.a - a.a ) * amount };

	}

	bool inside ( const rect& area, float x, float y ) {

		return x >= area.x && y >= area.y && x < area.x + area.w && y < area.y + area.h;

	}

	rect inset ( const rect& area, float amount ) {

		return { area.x + amount, area.y + amount, area.w - amount * 2, area.h - amount * 2 };

	}

	static D2D1_COLOR_F as_d2d ( color shade ) {

		return { shade.r, shade.g, shade.b, shade.a };

	}

	static D2D1_RECT_F as_d2d ( const rect& area ) {

		return { area.x, area.y, area.x + area.w, area.y + area.h };

	}

	static bool has_family ( IDWriteFactory* writer, const wchar_t* name ) {

		IDWriteFontCollection* fonts = nullptr;
		UINT32 index = 0;
		BOOL exists = FALSE;

		if ( FAILED ( writer->GetSystemFontCollection ( &fonts, FALSE ) ) || !fonts )
			return false;

		fonts->FindFamilyName ( name, &index, &exists );
		fonts->Release ( );

		return exists != FALSE;

	}

	static bool make_font ( renderer& item, font_kind kind, const wchar_t* family, float size, DWRITE_FONT_WEIGHT weight ) {

		IDWriteTextFormat* format = nullptr;

		if ( FAILED ( item.writer->CreateTextFormat ( family, nullptr, weight, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, size, L"en-us", &format ) ) )
			return false;

		format->SetWordWrapping ( DWRITE_WORD_WRAPPING_NO_WRAP );
		format->SetParagraphAlignment ( DWRITE_PARAGRAPH_ALIGNMENT_CENTER );

		IDWriteInlineObject* sign = nullptr;

		if ( SUCCEEDED ( item.writer->CreateEllipsisTrimmingSign ( format, &sign ) ) ) {

			DWRITE_TRIMMING trimming = { DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0 };

			format->SetTrimming ( &trimming, sign );

		}

		item.fonts [ kind ] = format;
		item.ellipsis [ kind ] = sign;
		item.sizes [ kind ] = size;

		return true;

	}

	bool start_renderer ( renderer& out ) {

		D2D1_FACTORY_OPTIONS options = { D2D1_DEBUG_LEVEL_NONE };

		if ( FAILED ( D2D1CreateFactory ( D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof ( ID2D1Factory ), &options, reinterpret_cast < void** > ( &out.factory ) ) ) )
			return false;

		if ( FAILED ( DWriteCreateFactory ( DWRITE_FACTORY_TYPE_SHARED, __uuidof ( IDWriteFactory ), reinterpret_cast < IUnknown** > ( &out.writer ) ) ) )
			return false;

		const wchar_t* code = has_family ( out.writer, L"Consolas" ) ? L"Consolas" : L"Courier New";
		const wchar_t* face = L"Segoe UI";

		bool fine = make_font ( out, FONT_UI, face, 14.0f, DWRITE_FONT_WEIGHT_NORMAL );

		fine = fine && make_font ( out, FONT_STRONG, face, 14.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD );
		fine = fine && make_font ( out, FONT_SMALL, face, 12.5f, DWRITE_FONT_WEIGHT_NORMAL );
		fine = fine && make_font ( out, FONT_CAPTION, face, 11.5f, DWRITE_FONT_WEIGHT_SEMI_BOLD );
		fine = fine && make_font ( out, FONT_CODE, code, 13.5f, DWRITE_FONT_WEIGHT_NORMAL );
		fine = fine && make_font ( out, FONT_CODE_SMALL, code, 12.5f, DWRITE_FONT_WEIGHT_NORMAL );
		fine = fine && make_font ( out, FONT_TITLE, face, 19.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD );
		fine = fine && make_font ( out, FONT_VALUE, face, 22.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD );
		fine = fine && make_font ( out, FONT_BRAND, face, 15.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD );

		if ( !fine )
			return false;

		out.ui_advance = measure ( out, FONT_UI, "abcdefghijklmnopqrstuvwxyz0123456789", 36 ) / 36.0f;
		out.code_advance = measure ( out, FONT_CODE, "MMMMMMMMMM", 10 ) / 10.0f;
		out.code_small_advance = measure ( out, FONT_CODE_SMALL, "MMMMMMMMMM", 10 ) / 10.0f;

		return true;

	}

	static void drop_target ( renderer& item ) {

		if ( item.brush )
			item.brush->Release ( );

		if ( item.window_target )
			item.window_target->Release ( );

		if ( item.dc_target )
			item.dc_target->Release ( );

		item.brush = nullptr;
		item.window_target = nullptr;
		item.dc_target = nullptr;
		item.target = nullptr;

	}

	void stop_renderer ( renderer& item ) {

		drop_target ( item );

		for ( uint32_t i = 0; i < FONT_COUNT; i++ ) {

			if ( item.ellipsis [ i ] )
				item.ellipsis [ i ]->Release ( );

			if ( item.fonts [ i ] )
				item.fonts [ i ]->Release ( );

			item.fonts [ i ] = nullptr;
			item.ellipsis [ i ] = nullptr;

		}

		if ( item.writer )
			item.writer->Release ( );

		if ( item.factory )
			item.factory->Release ( );

		item.writer = nullptr;
		item.factory = nullptr;

	}

	static bool make_window_target ( renderer& item ) {

		D2D1_RENDER_TARGET_PROPERTIES properties = { };
		D2D1_HWND_RENDER_TARGET_PROPERTIES window = { };

		properties.type = D2D1_RENDER_TARGET_TYPE_DEFAULT;
		properties.pixelFormat = { DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED };
		properties.dpiX = 96.0f * item.scale;
		properties.dpiY = 96.0f * item.scale;
		window.hwnd = item.window;
		window.pixelSize = { item.width ? item.width : 1, item.height ? item.height : 1 };
		window.presentOptions = D2D1_PRESENT_OPTIONS_NONE;

		if ( FAILED ( item.factory->CreateHwndRenderTarget ( &properties, &window, &item.window_target ) ) )
			return false;

		item.target = item.window_target;
		item.target->SetTextAntialiasMode ( D2D1_TEXT_ANTIALIAS_MODE_CLEARTYPE );

		return SUCCEEDED ( item.target->CreateSolidColorBrush ( { 1, 1, 1, 1 }, &item.brush ) );

	}

	bool attach_window ( renderer& item, HWND window, float scale ) {

		RECT client = { };

		GetClientRect ( window, &client );
		item.window = window;
		item.scale = scale;
		item.width = static_cast < uint32_t > ( client.right - client.left );
		item.height = static_cast < uint32_t > ( client.bottom - client.top );
		drop_target ( item );

		return make_window_target ( item );

	}

	void resize_window ( renderer& item, uint32_t width, uint32_t height, float scale ) {

		item.width = width;
		item.height = height;

		if ( scale != item.scale ) {

			item.scale = scale;
			drop_target ( item );
			make_window_target ( item );

			return;

		}

		if ( item.window_target )
			item.window_target->Resize ( { width ? width : 1, height ? height : 1 } );

	}

	bool begin_frame ( renderer& item ) {

		if ( !item.target && !make_window_target ( item ) )
			return false;

		item.clip_depth = 0;
		item.target->BeginDraw ( );
		item.target->SetTransform ( D2D1::Matrix3x2F::Identity ( ) );

		return true;

	}

	bool end_frame ( renderer& item ) {

		while ( item.clip_depth )
			pop_clip ( item );

		HRESULT result = item.target->EndDraw ( );

		if ( result == D2DERR_RECREATE_TARGET ) {

			drop_target ( item );

			return false;

		}

		return SUCCEEDED ( result );

	}

	bool capture_frame ( renderer& item, uint32_t width, uint32_t height, const char* path, void ( *draw ) ( void* context ), void* context ) {

		HDC screen = GetDC ( nullptr );
		HDC memory = CreateCompatibleDC ( screen );
		BITMAPINFO info = { };
		void* bits = nullptr;

		ReleaseDC ( nullptr, screen );
		info.bmiHeader.biSize = sizeof ( BITMAPINFOHEADER );
		info.bmiHeader.biWidth = static_cast < LONG > ( width );
		info.bmiHeader.biHeight = -static_cast < LONG > ( height );
		info.bmiHeader.biPlanes = 1;
		info.bmiHeader.biBitCount = 32;
		info.bmiHeader.biCompression = BI_RGB;

		HBITMAP bitmap = CreateDIBSection ( memory, &info, DIB_RGB_COLORS, &bits, nullptr, 0 );

		if ( !bitmap || !bits ) {

			DeleteDC ( memory );

			return false;

		}

		HGDIOBJ previous = SelectObject ( memory, bitmap );
		D2D1_RENDER_TARGET_PROPERTIES properties = { };

		properties.type = D2D1_RENDER_TARGET_TYPE_DEFAULT;
		properties.pixelFormat = { DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE };
		properties.dpiX = 96.0f * item.scale;
		properties.dpiY = 96.0f * item.scale;

		ID2D1RenderTarget* saved_target = item.target;
		ID2D1SolidColorBrush* saved_brush = item.brush;
		ID2D1DCRenderTarget* target = nullptr;
		bool fine = false;

		if ( SUCCEEDED ( item.factory->CreateDCRenderTarget ( &properties, &target ) ) ) {

			RECT whole = { 0, 0, static_cast < LONG > ( width ), static_cast < LONG > ( height ) };

			target->BindDC ( memory, &whole );
			target->SetTextAntialiasMode ( D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE );
			item.target = target;
			item.brush = nullptr;

			if ( SUCCEEDED ( target->CreateSolidColorBrush ( { 1, 1, 1, 1 }, &item.brush ) ) ) {

				uint32_t saved_width = item.width;
				uint32_t saved_height = item.height;

				item.width = width;
				item.height = height;
				item.clip_depth = 0;
				target->BeginDraw ( );
				draw ( context );

				while ( item.clip_depth )
					pop_clip ( item );

				fine = SUCCEEDED ( target->EndDraw ( ) );
				item.width = saved_width;
				item.height = saved_height;
				item.brush->Release ( );

			}

			target->Release ( );

		}

		item.target = saved_target;
		item.brush = saved_brush;

		if ( fine && path ) {

			GdiFlush ( );

			base::text png;

			fine = base::encode_png ( static_cast < const uint8_t* > ( bits ), width, height, width * 4, png );

			base::file_handle file;

			fine = fine && base::open_file ( file, path, true );

			if ( fine ) {

				fine = base::truncate_file ( file, 0 ) && base::write_at ( file, 0, png.value ( ), png.size );
				base::close_file ( file );

			}

		}

		SelectObject ( memory, previous );
		DeleteObject ( bitmap );
		DeleteDC ( memory );

		return fine;

	}

	void fill ( renderer& item, const rect& area, color shade ) {

		item.brush->SetColor ( as_d2d ( shade ) );
		item.target->FillRectangle ( as_d2d ( area ), item.brush );

	}

	void fill_round ( renderer& item, const rect& area, float radius, color shade ) {

		item.brush->SetColor ( as_d2d ( shade ) );
		item.target->FillRoundedRectangle ( { as_d2d ( area ), radius, radius }, item.brush );

	}

	void outline ( renderer& item, const rect& area, color shade, float width ) {

		float half = width * 0.5f;

		item.brush->SetColor ( as_d2d ( shade ) );
		item.target->DrawRectangle ( { area.x + half, area.y + half, area.x + area.w - half, area.y + area.h - half }, item.brush, width );

	}

	void outline_round ( renderer& item, const rect& area, float radius, color shade, float width ) {

		float half = width * 0.5f;

		item.brush->SetColor ( as_d2d ( shade ) );
		item.target->DrawRoundedRectangle ( { { area.x + half, area.y + half, area.x + area.w - half, area.y + area.h - half }, radius, radius }, item.brush, width );

	}

	void gradient ( renderer& item, const rect& area, color from, color to, float radius ) {

		D2D1_GRADIENT_STOP stops [ 2 ] = { { 0.0f, as_d2d ( from ) }, { 1.0f, as_d2d ( to ) } };
		ID2D1GradientStopCollection* collection = nullptr;
		ID2D1LinearGradientBrush* brush = nullptr;

		if ( FAILED ( item.target->CreateGradientStopCollection ( stops, 2, &collection ) ) )
			return;

		D2D1_LINEAR_GRADIENT_BRUSH_PROPERTIES line = { { area.x, area.y }, { area.x + area.w, area.y } };

		if ( SUCCEEDED ( item.target->CreateLinearGradientBrush ( line, collection, &brush ) ) ) {

			if ( radius > 0 )
				item.target->FillRoundedRectangle ( { as_d2d ( area ), radius, radius }, brush );
			else
				item.target->FillRectangle ( as_d2d ( area ), brush );

			brush->Release ( );

		}

		collection->Release ( );

	}

	void line ( renderer& item, float x1, float y1, float x2, float y2, color shade, float width ) {

		item.brush->SetColor ( as_d2d ( shade ) );
		item.target->DrawLine ( { x1, y1 }, { x2, y2 }, item.brush, width );

	}

	void curve ( renderer& item, float x1, float y1, float x2, float y2, color shade, float width ) {

		ID2D1PathGeometry* path = nullptr;
		ID2D1GeometrySink* sink = nullptr;

		if ( FAILED ( item.factory->CreatePathGeometry ( &path ) ) )
			return;

		if ( SUCCEEDED ( path->Open ( &sink ) ) ) {

			float bend = ( x2 - x1 ) * 0.5f;

			if ( bend < 40 && bend > -40 )
				bend = bend < 0 ? -40.0f : 40.0f;

			sink->BeginFigure ( { x1, y1 }, D2D1_FIGURE_BEGIN_HOLLOW );
			sink->AddBezier ( { { x1 + bend, y1 }, { x2 - bend, y2 }, { x2, y2 } } );
			sink->EndFigure ( D2D1_FIGURE_END_OPEN );
			sink->Close ( );
			sink->Release ( );
			item.brush->SetColor ( as_d2d ( shade ) );
			item.target->DrawGeometry ( path, item.brush, width );

		}

		path->Release ( );

	}

	void loop_curve ( renderer& item, float x1, float y1, float x2, float y2, float offset, color shade, float width ) {

		ID2D1PathGeometry* path = nullptr;
		ID2D1GeometrySink* sink = nullptr;

		if ( FAILED ( item.factory->CreatePathGeometry ( &path ) ) )
			return;

		if ( SUCCEEDED ( path->Open ( &sink ) ) ) {

			sink->BeginFigure ( { x1, y1 }, D2D1_FIGURE_BEGIN_HOLLOW );
			sink->AddBezier ( { { x1 + offset, y1 }, { x2 + offset, y2 }, { x2, y2 } } );
			sink->EndFigure ( D2D1_FIGURE_END_OPEN );
			sink->Close ( );
			sink->Release ( );
			item.brush->SetColor ( as_d2d ( shade ) );
			item.target->DrawGeometry ( path, item.brush, width );

		}

		path->Release ( );

	}

	void circle ( renderer& item, float x, float y, float radius, color shade ) {

		item.brush->SetColor ( as_d2d ( shade ) );
		item.target->FillEllipse ( { { x, y }, radius, radius }, item.brush );

	}

	void ring ( renderer& item, float x, float y, float radius, color shade, float width ) {

		item.brush->SetColor ( as_d2d ( shade ) );
		item.target->DrawEllipse ( { { x, y }, radius, radius }, item.brush, width );

	}

	static uint32_t widen ( renderer& item, const char* value, size_t size ) {

		if ( !size )
			return 0;

		int32_t count = MultiByteToWideChar ( CP_UTF8, 0, value, static_cast < int > ( size < RENDER_TEXT_ROOM / 2 ? size : RENDER_TEXT_ROOM / 2 ), item.wide, RENDER_TEXT_ROOM - 1 );

		return count > 0 ? static_cast < uint32_t > ( count ) : 0;

	}

	void text ( renderer& item, font_kind font, const char* value, size_t size, const rect& area, color shade, align_kind align ) {

		uint32_t count = widen ( item, value, size );

		if ( !count || area.w <= 0 )
			return;

		IDWriteTextLayout* layout = nullptr;

		if ( FAILED ( item.writer->CreateTextLayout ( item.wide, count, item.fonts [ font ], area.w, area.h, &layout ) ) )
			return;

		layout->SetTextAlignment ( align == ALIGN_CENTER ? DWRITE_TEXT_ALIGNMENT_CENTER : align == ALIGN_RIGHT ? DWRITE_TEXT_ALIGNMENT_TRAILING : DWRITE_TEXT_ALIGNMENT_LEADING );
		item.brush->SetColor ( as_d2d ( shade ) );
		item.target->DrawTextLayout ( { area.x, area.y }, layout, item.brush, D2D1_DRAW_TEXT_OPTIONS_CLIP );
		layout->Release ( );

	}

	void text ( renderer& item, font_kind font, const char* value, const rect& area, color shade, align_kind align ) {

		text ( item, font, value, base::length ( value ), area, shade, align );

	}

	float measure ( renderer& item, font_kind font, const char* value, size_t size ) {

		uint32_t count = widen ( item, value, size );

		if ( !count )
			return 0;

		IDWriteTextLayout* layout = nullptr;
		DWRITE_TEXT_METRICS metrics = { };

		if ( FAILED ( item.writer->CreateTextLayout ( item.wide, count, item.fonts [ font ], 100000.0f, 100.0f, &layout ) ) )
			return 0;

		layout->GetMetrics ( &metrics );
		layout->Release ( );

		return metrics.widthIncludingTrailingWhitespace;

	}

	float measure ( renderer& item, font_kind font, const char* value ) {

		return measure ( item, font, value, base::length ( value ) );

	}

	void push_clip ( renderer& item, const rect& area ) {

		if ( item.clip_depth == RENDER_CLIP_DEPTH )
			return;

		item.clips [ item.clip_depth++ ] = area;
		item.target->PushAxisAlignedClip ( as_d2d ( area ), D2D1_ANTIALIAS_MODE_ALIASED );

	}

	void pop_clip ( renderer& item ) {

		if ( !item.clip_depth )
			return;

		item.clip_depth--;
		item.target->PopAxisAlignedClip ( );

	}

}
