#pragma once

#include "base/text/text.hh"

#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>

#define RENDER_TEXT_ROOM 4096
#define RENDER_CLIP_DEPTH 32

namespace ui {

	enum font_kind : uint8_t {

		FONT_UI,
		FONT_STRONG,
		FONT_SMALL,
		FONT_CAPTION,
		FONT_CODE,
		FONT_CODE_SMALL,
		FONT_TITLE,
		FONT_VALUE,
		FONT_BRAND,
		FONT_COUNT,

	};

	enum align_kind : uint8_t {

		ALIGN_LEFT,
		ALIGN_CENTER,
		ALIGN_RIGHT,

	};

	struct color {

		float r;
		float g;
		float b;
		float a;

	};

	struct rect {

		float x;
		float y;
		float w;
		float h;

	};

	struct renderer {

		ID2D1Factory* factory;
		IDWriteFactory* writer;
		ID2D1RenderTarget* target;
		ID2D1HwndRenderTarget* window_target;
		ID2D1DCRenderTarget* dc_target;
		ID2D1SolidColorBrush* brush;
		IDWriteTextFormat* fonts [ FONT_COUNT ];
		IDWriteInlineObject* ellipsis [ FONT_COUNT ];
		float sizes [ FONT_COUNT ];
		float ui_advance;
		float code_advance;
		float code_small_advance;
		float scale;
		HWND window;
		uint32_t width;
		uint32_t height;
		wchar_t wide [ RENDER_TEXT_ROOM ];
		rect clips [ RENDER_CLIP_DEPTH ];
		uint32_t clip_depth;

	};

	color rgb ( uint32_t value, float alpha = 1.0f );

	color mix ( color a, color b, float amount );

	bool inside ( const rect& area, float x, float y );

	rect inset ( const rect& area, float amount );

	bool start_renderer ( renderer& out );

	void stop_renderer ( renderer& item );

	bool attach_window ( renderer& item, HWND window, float scale );

	void resize_window ( renderer& item, uint32_t width, uint32_t height, float scale );

	bool begin_frame ( renderer& item );

	bool end_frame ( renderer& item );

	bool capture_frame ( renderer& item, uint32_t width, uint32_t height, const char* path, void ( *draw ) ( void* context ), void* context );

	void fill ( renderer& item, const rect& area, color shade );

	void fill_round ( renderer& item, const rect& area, float radius, color shade );

	void outline ( renderer& item, const rect& area, color shade, float width = 1.0f );

	void outline_round ( renderer& item, const rect& area, float radius, color shade, float width = 1.0f );

	void gradient ( renderer& item, const rect& area, color from, color to, float radius = 0.0f );

	void line ( renderer& item, float x1, float y1, float x2, float y2, color shade, float width = 1.0f );

	void curve ( renderer& item, float x1, float y1, float x2, float y2, color shade, float width = 1.0f );

	void loop_curve ( renderer& item, float x1, float y1, float x2, float y2, float offset, color shade, float width = 1.0f );

	void circle ( renderer& item, float x, float y, float radius, color shade );

	void ring ( renderer& item, float x, float y, float radius, color shade, float width = 1.0f );

	void text ( renderer& item, font_kind font, const char* value, size_t size, const rect& area, color shade, align_kind align = ALIGN_LEFT );

	void text ( renderer& item, font_kind font, const char* value, const rect& area, color shade, align_kind align = ALIGN_LEFT );

	float measure ( renderer& item, font_kind font, const char* value, size_t size );

	float measure ( renderer& item, font_kind font, const char* value );

	void push_clip ( renderer& item, const rect& area );

	void pop_clip ( renderer& item );

}
