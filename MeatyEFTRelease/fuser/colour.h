#pragma once

#include <d3d9.h>
#pragma comment(lib,"d3d9.lib")

#include <stdint.h>
#include <cmath>

//rgba float[4] to color_t, it should be faster than color_t(float*) constructor 
#define PF2COL(color) reinterpret_cast<color_t>(( \
(((color[3] * 255.f) & 0xff) << 24) | (((color[0] * 255.f) & 0xff) << 16) | \
(((color[1] * 255.f) & 0xff) << 8) | ((color[2] * 255.f) & 0xff)))

struct color_t
{
	D3DCOLOR color;

	inline uint8_t* a() { return reinterpret_cast<uint8_t*>(&color + 3); }
	inline uint8_t* r() { return reinterpret_cast<uint8_t*>(&color + 2); }
	inline uint8_t* g() { return reinterpret_cast<uint8_t*>(&color + 1); }
	inline uint8_t* b() { return reinterpret_cast<uint8_t*>(&color + 0); }

	inline const float get_a() { return *a() / 255.f; }
	inline const float get_r() { return *r() / 255.f; }
	inline const float get_g() { return *g() / 255.f; }
	inline const float get_b() { return *b() / 255.f; }

	inline void set_a(const float a) { reinterpret_cast<uint8_t*>(&color)[3] = static_cast<uint8_t>(a * 255.f); }
	inline void set_r(const float r) { reinterpret_cast<uint8_t*>(&color)[2] = static_cast<uint8_t>(r * 255.f); }
	inline void set_g(const float g) { reinterpret_cast<uint8_t*>(&color)[1] = static_cast<uint8_t>(g * 255.f); }
	inline void set_b(const float b) { reinterpret_cast<uint8_t*>(&color)[0] = static_cast<uint8_t>(b * 255.f); }

	color_t() : color(0) {}
	color_t(int value) : color(value) {}
	color_t(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 0xFF) : color(D3DCOLOR_RGBA(r, g, b, a)) {}
	color_t(float* value) : color(D3DCOLOR_COLORVALUE(value[0], value[1], value[2], value[3])) {}

	inline color_t& operator=(const int& rhs)
	{
		color = rhs;
		return *this;
	}
	inline color_t& operator=(const D3DCOLOR& rhs)
	{
		color = rhs;
		return *this;
	}
	inline color_t& operator=(const float* rhs)
	{
		color = D3DCOLOR_COLORVALUE(rhs[0], rhs[1], rhs[2], rhs[3]);
		return *this;
	}
	inline operator const D3DCOLOR() const { return color; }
	inline operator const uint8_t* () const { return (uint8_t*)this; }

	void SetHSV(float h, float s, float v, float a = 1.f)
	{
		if (s == 0.f)// gray
		{
			color = D3DCOLOR_COLORVALUE(v, v, v, a);
			return;
		}

		h = fmodf(h, 1.f) / (60.f / 360.f);

		const int i = static_cast<int>(h);
		const float f = h - static_cast<float>(i);
		const float q = v * (1.f - s * f);
		const float t = v * (1.f - s * (1.0f - f));
		const float p = v * (1.f - s);

		float r, g, b;

		switch (i)
		{
		case 0: r = v; g = t; b = p; break;
		case 1: r = q; g = v; b = p; break;
		case 2: r = p; g = v; b = t; break;
		case 3: r = p; g = q; b = v; break;
		case 4: r = t; g = p; b = v; break;
		default: r = v; g = p; b = q; break;
		}

		color = D3DCOLOR_COLORVALUE(r, g, b, a);
	}
};

#define DEF_COLOR(value, name) namespace Colors { static const color_t name = color_t(value); }

DEF_COLOR(0xFF000000, Black);
DEF_COLOR(0xFFFFFFFF, White);

DEF_COLOR(0xFFFF0000, Red);
DEF_COLOR(0xFF00FF00, Green);
DEF_COLOR(0xFF0000FF, Blue);

DEF_COLOR(0xFFFFFF00, Yellow);
DEF_COLOR(0xFF00FFFF, SkyBlue);
DEF_COLOR(0xFFFF00FF, Pink);