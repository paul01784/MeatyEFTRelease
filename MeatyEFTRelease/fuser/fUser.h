#pragma once //TAKES PLACE OF (#ifndef guards)

#include "fUser_includes.h"
#include <map>
#include <vector>
#include <DirectXMath.h>
#include <algorithm>

#include "../app/globals.h"
#include "colour.h"


struct Vertex_t;
#define ALPHA_MASK 0xFF000000
#define IM_PI 3.14159265358979323846f


//fps stuff
// Variables for FPS calculation and limiting
extern LARGE_INTEGER frequency, lastTime, currentTime;
extern double deltaTime;
extern double frameTime;
extern int frameCount;
extern double timeElapsed ;


struct Point
{
	float x;
	float y;
};



enum RenderDrawType : uint32_t
{
	RenderDrawType_None = 0,
	RenderDrawType_Outlined = 1 << 0,
	RenderDrawType_Filled = 1 << 1,
	RenderDrawType_Gradient = 1 << 2,
	RenderDrawType_OutlinedGradient = RenderDrawType_Outlined | RenderDrawType_Gradient,
	RenderDrawType_FilledGradient = RenderDrawType_Filled | RenderDrawType_Gradient
};

//Abstract application class
class DXApp
{
public:
	//Constructor
	DXApp();
	//Destructor
	virtual ~DXApp(void);

	//Main application loop
	int Run();

	//Framework methods
	 bool Init();
	 virtual void Update(float dt) = 0; //pure virtual, aka MUST be overridden by inheriting class
	 virtual void Render() = 0; //pure virtual, aka MUST be overridden by inheriting class
	 LRESULT MsgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam); //message procedure function

	 virtual void OnLostDevice() = 0;  //Handle lost graphics
	 virtual void OnResetDevice() = 0; //Handle reset graphics
	 bool CleanUp();

	 void EndDraw();
	 void BeginDraw();
	 void PushRenderState(const D3DRENDERSTATETYPE dwState, DWORD dwValue);

	 bool LoadFont();
	 void HandleResize();
	 D3DXVECTOR2 GetTextSize(ID3DXFont* pFont, const char* text);

	 void RenderText(const char* text, int x, int y, D3DCOLOR color, bool center = false);
	 void DrawLine(int16_t x1, int16_t y1, int16_t x2, int16_t y2, color_t color, int16_t thickness);
	 void DrawFilledBox(int16_t x, int16_t y, int16_t width, int16_t height, color_t color);
	 void DrawBox(int16_t x, int16_t y, int16_t width, int16_t height, int16_t thickness, color_t color);
	 void DrawBox(int16_t x, int16_t y, int16_t width, int16_t height, color_t color);
	 void DrawCornerBox(int16_t x, int16_t y, int16_t width, int16_t height, int16_t thickness, color_t color);
	//virtual void DrawCircle(int16_t x, int16_t y, int16_t radius, uint16_t points, float thickness, RenderDrawType flags, color_t color1, color_t color2);
	 void AddCircle(float centerX, float centerY, float radius, color_t color, int numSegments, float thickness);
	 void DrawBoneLine(glm::vec2 from, glm::vec2 to, color_t color, float thickness);


	D3DVIEWPORT9 viewport;

protected:
	//Members

	HWND			m_hAppWindow;				//HANDLE to application window
	HINSTANCE	m_hAppInstance;			//HANDLE to application instance
	UINT			m_ClientWidth;			//Requested client width
	UINT			m_ClientHeight;			//Requested client height
	std::string	m_AppTitle;				//Application title (window title bar)
	DWORD		m_WindowStyle;			//Window style (e.g. WS_OVERLAPPEDWINDOW)
	bool			m_Paused;				//True if application pause, false otherwise
	bool			m_EnableFullscreen;		//True to enable fullscreen, false otherwise
	float		m_FPS;					//Frames per second of our application

	UINT g_requestedWidth = 0;
	UINT g_requestedHeight = 0;

	//DirectX members
	IDirect3D9* m_pDirect3D;			//Direct3D interface
	IDirect3DDevice9* m_pDevice3D;			//Direct3D device interface
	D3DPRESENT_PARAMETERS		m_d3dpp;				//Direct3D present parameters struct
	D3DDISPLAYMODE			m_Mode;				//Direct3D display mode struct
	D3DDEVTYPE				m_DevType;			//Device Type (SHOULD BE DEVTYPE_HAL)


	//Font members
	LPD3DXFONT g_pFont; // Font object

	struct SinCos_t
	{
		float flSin = 0.f, flCos = 0.f;
	};

	struct RenderState_t
	{
		D3DRENDERSTATETYPE dwState;
		DWORD dwValue;
	};

	struct Vertex_t
	{
		Vertex_t() { }

		Vertex_t(int _x, int _y, color_t _color)
		{
			x = static_cast<float>(_x);
			y = static_cast<float>(_y);
			z = 0;
			rhw = 1;
			color = _color.color;
		}

		Vertex_t(float _x, float _y, color_t _color)
		{
			x = _x;
			y = _y;
			z = 0;
			rhw = 1;
			color = _color.color;
		}

		float x, y, z, rhw;
		color_t color = 0;
	};

	std::map<uint16_t, SinCos_t*> m_SinCosContainer;

	//we dont need to calculate sin and cos every frame, we just calculate it one time
	SinCos_t* GetSinCos(uint16_t key)
	{
		if (!m_SinCosContainer.count(key))
		{
			SinCos_t* temp_array = new SinCos_t[key + 1];

			uint16_t i = 0;
			for (float angle = 0.0; angle <= 2 * D3DX_PI; angle += (2 * D3DX_PI) / key)
				temp_array[i++] = SinCos_t{ sin(angle), cos(angle) };

			m_SinCosContainer.insert(std::pair<uint16_t, SinCos_t*>(key, temp_array));
		}

		return m_SinCosContainer[key];
	}

	bool m_bUseDynamicSinCos;
	IDirect3DDevice9* m_pDevice;
	std::vector<RenderState_t> m_RenderStates;

protected:
	//Methods

	//Initializes main application window
	bool InitMainWindow();
	//Initialize direct3D
	bool InitDirect3D();
	//Handles lost device
	bool IsDeviceLost();
	//Calculates FPS
	void CalculateFPS(float dt);
	//Enables fullscreen
	void EnableFullscreen(bool enable);
};
