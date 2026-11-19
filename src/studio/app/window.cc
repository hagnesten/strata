#include "studio/app/studio.hh"
#include "base/file/file.hh"

#include <windows.h>
#include <shellapi.h>
#include <dwmapi.h>

namespace studio {

	static void request_frame ( studio_state& app ) {

		InvalidateRect ( app.window, nullptr, FALSE );

	}

	void finish_input ( studio_state& app ) {

		input_state& input = app.input;

		if ( !input.down )
			app.ui.active = 0;

		input.pressed = false;
		input.released = false;
		input.double_click = false;
		input.right_pressed = false;
		input.wheel = 0;
		input.key_count = 0;
		input.char_count = 0;

		if ( app.ui.redraw ) {

			app.ui.redraw = false;

			if ( app.window )
				request_frame ( app );

		}

	}

	static void render ( studio_state& app ) {

		if ( !ui::begin_frame ( app.draw ) )
			return;

		draw_frame ( app );
		ui::end_frame ( app.draw );
		finish_input ( app );

	}

	static void modifiers ( input_state& input ) {

		input.shift = GetKeyState ( VK_SHIFT ) < 0;
		input.ctrl = GetKeyState ( VK_CONTROL ) < 0;
		input.alt = GetKeyState ( VK_MENU ) < 0;

	}

	static LRESULT CALLBACK window_proc ( HWND window, UINT message, WPARAM wide, LPARAM lparam ) {

		studio_state* app = reinterpret_cast < studio_state* > ( GetWindowLongPtrW ( window, GWLP_USERDATA ) );

		if ( !app || !app->draw.factory || !app->draw.target )
			return DefWindowProcW ( window, message, wide, lparam );

		input_state& input = app->input;
		float scale = app->draw.scale > 0 ? app->draw.scale : 1.0f;

		switch ( message ) {

			case WM_NCCALCSIZE:
				if ( wide ) {

					NCCALCSIZE_PARAMS* sizes = reinterpret_cast < NCCALCSIZE_PARAMS* > ( lparam );

					if ( IsZoomed ( window ) ) {

						UINT dpi = GetDpiForWindow ( window );
						int32_t frame = GetSystemMetricsForDpi ( SM_CXFRAME, dpi ) + GetSystemMetricsForDpi ( SM_CXPADDEDBORDER, dpi );

						sizes->rgrc [ 0 ].left += frame;
						sizes->rgrc [ 0 ].right -= frame;
						sizes->rgrc [ 0 ].top += frame;
						sizes->rgrc [ 0 ].bottom -= frame;

					}

					return 0;

				}

				break;

			case WM_NCHITTEST: {

				POINT point = { static_cast < int16_t > ( LOWORD ( lparam ) ), static_cast < int16_t > ( HIWORD ( lparam ) ) };
				RECT client;

				ScreenToClient ( window, &point );
				GetClientRect ( window, &client );

				int32_t edge = static_cast < int32_t > ( 6 * scale );
				bool zoomed = IsZoomed ( window ) != FALSE;
				bool left = point.x < edge;
				bool right = point.x >= client.right - edge;
				bool top = point.y < edge;
				bool bottom = point.y >= client.bottom - edge;

				if ( !zoomed ) {

					if ( top && left ) return HTTOPLEFT;
					if ( top && right ) return HTTOPRIGHT;
					if ( bottom && left ) return HTBOTTOMLEFT;
					if ( bottom && right ) return HTBOTTOMRIGHT;
					if ( left ) return HTLEFT;
					if ( right ) return HTRIGHT;
					if ( top ) return HTTOP;
					if ( bottom ) return HTBOTTOM;

				}

				float x = static_cast < float > ( point.x ) / scale;
				float y = static_cast < float > ( point.y ) / scale;
				float width = static_cast < float > ( client.right ) / scale;
				float tools = LAYOUT_WINDOW_BUTTON * 3 + 14 + icon_button_width ( app->draw, "Open" );

				if ( y < LAYOUT_TITLE && x < width - tools )
					return HTCAPTION;

				return HTCLIENT;

			}

			case WM_MOUSEMOVE:
				input.x = static_cast < float > ( static_cast < int16_t > ( LOWORD ( lparam ) ) ) / scale;
				input.y = static_cast < float > ( static_cast < int16_t > ( HIWORD ( lparam ) ) ) / scale;
				modifiers ( input );
				request_frame ( *app );

				return 0;

			case WM_LBUTTONDOWN:
			case WM_LBUTTONDBLCLK:
				input.x = static_cast < float > ( static_cast < int16_t > ( LOWORD ( lparam ) ) ) / scale;
				input.y = static_cast < float > ( static_cast < int16_t > ( HIWORD ( lparam ) ) ) / scale;
				input.pressed = true;
				input.down = true;
				input.double_click = message == WM_LBUTTONDBLCLK;
				modifiers ( input );
				SetCapture ( window );
				render ( *app );

				return 0;

			case WM_LBUTTONUP:
				input.released = true;
				input.down = false;
				modifiers ( input );
				ReleaseCapture ( );
				render ( *app );

				return 0;

			case WM_RBUTTONDOWN:
				input.right_pressed = true;
				request_frame ( *app );

				return 0;

			case WM_MOUSEWHEEL: {

				POINT point = { static_cast < int16_t > ( LOWORD ( lparam ) ), static_cast < int16_t > ( HIWORD ( lparam ) ) };

				ScreenToClient ( window, &point );
				input.x = static_cast < float > ( point.x ) / scale;
				input.y = static_cast < float > ( point.y ) / scale;
				input.wheel += static_cast < float > ( static_cast < int16_t > ( HIWORD ( wide ) ) ) / WHEEL_DELTA;
				modifiers ( input );
				render ( *app );

				return 0;

			}

			case WM_KEYDOWN:
			case WM_SYSKEYDOWN:
				modifiers ( input );

				if ( input.key_count < INPUT_KEYS )
					input.keys [ input.key_count++ ] = static_cast < uint32_t > ( wide );

				render ( *app );

				if ( message == WM_SYSKEYDOWN && wide != VK_F10 )
					return DefWindowProcW ( window, message, wide, lparam );

				return 0;

			case WM_CHAR: {

				uint32_t code = static_cast < uint32_t > ( wide );

				if ( code >= 0xD800 && code < 0xDC00 ) {

					app->surrogate = code;

					return 0;

				}

				if ( code >= 0xDC00 && code < 0xE000 && app->surrogate )
					code = 0x10000 + ( ( app->surrogate - 0xD800 ) << 10 ) + ( code - 0xDC00 );

				app->surrogate = 0;
				modifiers ( input );

				if ( input.char_count < INPUT_CHARS )
					input.chars [ input.char_count++ ] = code;

				render ( *app );

				return 0;

			}

			case WM_SIZE:
				app->maximized = wide == SIZE_MAXIMIZED;
				ui::resize_window ( app->draw, LOWORD ( lparam ), HIWORD ( lparam ), scale );
				request_frame ( *app );

				return 0;

			case WM_ACTIVATE:
				request_frame ( *app );

				break;

			case WM_DPICHANGED: {

				RECT* suggested = reinterpret_cast < RECT* > ( lparam );

				app->draw.scale = static_cast < float > ( HIWORD ( wide ) ) / 96.0f;
				ui::attach_window ( app->draw, window, app->draw.scale );
				SetWindowPos ( window, nullptr, suggested->left, suggested->top, suggested->right - suggested->left, suggested->bottom - suggested->top, SWP_NOZORDER | SWP_NOACTIVATE );

				return 0;

			}

			case WM_SETCURSOR:
				if ( LOWORD ( lparam ) == HTCLIENT ) {

					const wchar_t* shape = IDC_ARROW;

					switch ( app->ui.cursor ) {

						case CURSOR_TEXT: shape = IDC_IBEAM; break;
						case CURSOR_SIZE_WE: shape = IDC_SIZEWE; break;
						case CURSOR_SIZE_NS: shape = IDC_SIZENS; break;
						case CURSOR_HAND: shape = IDC_HAND; break;
						case CURSOR_MOVE: shape = IDC_SIZEALL; break;
						default: break;

					}

					SetCursor ( LoadCursorW ( nullptr, shape ) );

					return TRUE;

				}

				break;

			case WM_PAINT: {

				PAINTSTRUCT paint;

				BeginPaint ( window, &paint );
				render ( *app );
				EndPaint ( window, &paint );

				return 0;

			}

			case WM_ERASEBKGND:
				return 1;

			case WM_GETMINMAXINFO: {

				MINMAXINFO* limits = reinterpret_cast < MINMAXINFO* > ( lparam );

				limits->ptMinTrackSize.x = static_cast < LONG > ( 980 * scale );
				limits->ptMinTrackSize.y = static_cast < LONG > ( 620 * scale );

				return 0;

			}

			case WM_DROPFILES: {

				HDROP drop = reinterpret_cast < HDROP > ( wide );
				wchar_t name [ MAX_PATH ];

				if ( DragQueryFileW ( drop, 0, name, MAX_PATH ) ) {

					char path [ FILE_PATH_ROOM ];

					if ( WideCharToMultiByte ( CP_UTF8, 0, name, -1, path, sizeof ( path ), nullptr, nullptr ) > 0 )
						open_database_file ( *app, path );

				}

				DragFinish ( drop );
				request_frame ( *app );

				return 0;

			}

			case WM_DESTROY:
				PostQuitMessage ( 0 );

				return 0;

			default:
				break;

		}

		return DefWindowProcW ( window, message, wide, lparam );

	}

	studio_state* make_state ( ) {

		studio_state* app = base::make < studio_state > ( 1 );

		if ( !app )
			return nullptr;

		LARGE_INTEGER frequency;

		QueryPerformanceFrequency ( &frequency );
		app->frequency = static_cast < uint64_t > ( frequency.QuadPart );
		app->sidebar_width = LAYOUT_SIDEBAR;
		app->editor_height = 250;
		app->table = -1;
		app->trees_for = -1;
		app->view = VIEW_DATA;
		app->dragging_card = -1;
		app->editor.id = make_id ( "editor" );
		app->editor.marked = -1;
		clear_grid ( app->data_grid );
		clear_grid ( app->result_grid );
		app->data_grid.id = make_id ( "grid.data" );
		app->result_grid.id = make_id ( "grid.result" );
		set_editor_text ( app->editor, "SELECT c.name, count(o.id) AS orders, round(sum(i.quantity * p.price), 2) AS spent\nFROM customers c\nJOIN orders o ON o.customer = c.id\nJOIN order_items i ON i.order_id = o.id\nJOIN products p ON p.id = i.product\nGROUP BY c.id\nORDER BY spent DESC\nLIMIT 50;" );
		start_tabs ( *app );

		return app;

	}

	static bool sample_path ( char* out, size_t room ) {

		base::text path;

		if ( !documents_folder ( path ) )
			return false;

		path.add ( "\\sample.sdb" );
		base::copy_text ( out, room, path.value ( ) );

		return true;

	}

	bool prepare_sample ( studio_state& app, const char* path ) {

		bool fresh = !base::path_exists ( path );

		if ( !open_database_file ( app, path ) )
			return false;

		if ( fresh || !app.db->schema->table_count ) {

			if ( !build_sample ( *app.db ) ) {

				set_status ( app, app.db->problem.value ( ), true );

				return false;

			}

			refresh_schema ( app );
			select_table ( app, 0 );
			set_status ( app, "Made a sample database to explore", false );

		}

		return true;

	}

	int32_t run_studio ( const char* path ) {

		SetProcessDpiAwarenessContext ( DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 );

		studio_state* app = make_state ( );

		if ( !app || !ui::start_renderer ( app->draw ) )
			return 1;

		load_settings ( *app );

		HINSTANCE instance = GetModuleHandleW ( nullptr );
		WNDCLASSEXW kind = { };

		kind.cbSize = sizeof ( kind );
		kind.style = CS_DBLCLKS;
		kind.lpfnWndProc = window_proc;
		kind.hInstance = instance;
		kind.hCursor = LoadCursorW ( nullptr, IDC_ARROW );
		kind.hIcon = LoadIconW ( instance, MAKEINTRESOURCEW ( 1 ) );
		kind.lpszClassName = L"strata_studio";
		RegisterClassExW ( &kind );

		POINT origin = { 0, 0 };
		HMONITOR monitor = MonitorFromPoint ( origin, MONITOR_DEFAULTTOPRIMARY );
		MONITORINFO area = { sizeof ( area ) };
		UINT dpi = GetDpiForSystem ( );
		float scale = static_cast < float > ( dpi ) / 96.0f;

		GetMonitorInfoW ( monitor, &area );

		int32_t width = static_cast < int32_t > ( 1440 * scale );
		int32_t height = static_cast < int32_t > ( 900 * scale );
		int32_t work_w = area.rcWork.right - area.rcWork.left;
		int32_t work_h = area.rcWork.bottom - area.rcWork.top;

		if ( width > work_w - 40 )
			width = work_w - 40;

		if ( height > work_h - 40 )
			height = work_h - 40;

		HWND window = CreateWindowExW ( WS_EX_ACCEPTFILES, L"strata_studio", L"Strata Studio", WS_OVERLAPPEDWINDOW, area.rcWork.left + ( work_w - width ) / 2, area.rcWork.top + ( work_h - height ) / 2, width, height, nullptr, nullptr, instance, nullptr );

		if ( !window )
			return 1;

		BOOL dark = TRUE;
		COLORREF caption = RGB ( 0x0E, 0x0E, 0x11 );
		COLORREF text = RGB ( 0xE3, 0xE3, 0xE8 );

		MARGINS margins = { 0, 0, 1, 0 };

		DwmSetWindowAttribute ( window, 20, &dark, sizeof ( dark ) );
		DwmSetWindowAttribute ( window, 35, &caption, sizeof ( caption ) );
		DwmSetWindowAttribute ( window, 36, &text, sizeof ( text ) );
		DwmExtendFrameIntoClientArea ( window, &margins );

		app->window = window;
		ui::attach_window ( app->draw, window, static_cast < float > ( GetDpiForWindow ( window ) ) / 96.0f );
		SetWindowLongPtrW ( window, GWLP_USERDATA, reinterpret_cast < LONG_PTR > ( app ) );
		SetWindowPos ( window, nullptr, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE );

		if ( path && *path )
			open_database_file ( *app, path );
		else {

			char sample [ FILE_PATH_ROOM ];

			if ( sample_path ( sample, sizeof ( sample ) ) )
				prepare_sample ( *app, sample );

		}

		ShowWindow ( window, SW_SHOWDEFAULT );
		UpdateWindow ( window );

		MSG message;

		while ( GetMessageW ( &message, nullptr, 0, 0 ) > 0 ) {

			TranslateMessage ( &message );
			DispatchMessageW ( &message );

		}

		if ( app->db )
			engine::close_database ( *app->db );

		ui::stop_renderer ( app->draw );

		return 0;

	}

}