#include "fUser.h"
#include "../app/fuserapp.h"
#include <chrono>

// Variables for FPS calculation and limiting
LARGE_INTEGER frequency, lastTime, currentTime;
double deltaTime = 0.0;
double frameTime = 1.0 / globals::appFuserMaxFPS; // Target time per frame (60 FPS)
int frameCount = 0;
double timeElapsed = 0.0;


namespace
{
	//Create global pointer to a DXApp object
	//This will be used to forward messages from a global space to
	//our user define message procedure. This is necessary due to the fact that
	//we cant create a method defined as WNDPROC.
	DXApp* g_pApp = NULL;
}

//GLOBAL MESSAGE PROCEDURE
//This is used to forward messages to our used defined procedure function
LRESULT CALLBACK MainWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	//Forward messages
	if (g_pApp)
		return (g_pApp->MsgProc(hwnd, msg, wParam, lParam));
	else
		return DefWindowProc(hwnd, msg, wParam, lParam);
}

DXApp::DXApp()
{
	//Initialize members
	m_hAppInstance = NULL;
	m_hAppWindow = NULL;
	m_AppTitle = "MeatyEFT FUSER";
	m_ClientWidth = 800;
	m_ClientHeight = 600;
	m_EnableFullscreen = false; //not used yet anyway
	m_Paused = false; //application starts unpaused
	m_FPS = 0;
	m_WindowStyle = WS_OVERLAPPEDWINDOW; //Standard non-resizeable window
	g_pApp = this; //Set our global pointer

	m_pDirect3D = 0;
	m_pDevice3D = 0;
	m_DevType = D3DDEVTYPE_HAL;



}

DXApp::~DXApp(void)
{
	//Release objects from memory
	SAFE_RELEASE(m_pDevice3D);
	SAFE_RELEASE(m_pDirect3D);
	SAFE_DELETE(g_pApp);
	SAFE_RELEASE(g_pFont);
}

int DXApp::Run()
{
	
	MSG msg = { 0 };

	// Initialize performance counter
	QueryPerformanceFrequency(&frequency);
	QueryPerformanceCounter(&lastTime);


	while (WM_QUIT != msg.message)
	{
		if (!espGlobals::runEsp)
			return 0;

		if (PeekMessage(&msg, NULL, NULL, NULL, PM_REMOVE))
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
		else
		{
			if (!IsDeviceLost())
			{
				// Start timing
				auto frameStartTime = std::chrono::high_resolution_clock::now();

				// Render
				try
				{
					Render();
				}
				catch (...)
				{
				}
				// Calculate elapsed time
				auto frameEndTime = std::chrono::high_resolution_clock::now();
				std::chrono::duration<double> elapsedTime = frameEndTime - frameStartTime;

				// Target frame time in seconds
				double frameTime = 1.0 / globals::appFuserMaxFPS;

				// Busy-wait loop until the next frame time
				while (elapsedTime.count() < frameTime)
				{
					frameEndTime = std::chrono::high_resolution_clock::now();
					elapsedTime = frameEndTime - frameStartTime;
				}

				// FPS calculation
				timeElapsed += elapsedTime.count();
				frameCount++;

				if (timeElapsed >= 1.0) // After 1 second, calculate FPS
				{
					m_FPS = frameCount;
					frameCount = 0;
					timeElapsed = 0.0;
				}
			}
		}
	}

	espGlobals::runEsp = false;

	return static_cast<int>(msg.wParam);
}

bool DXApp::Init()
{
	//Initialize main window
	if (!InitMainWindow())
		return false;
	if (!InitDirect3D())
		return false;
	if (!LoadFont())
		return false;

	//If all succeeds return true
	return true;
}

bool DXApp::CleanUp()
{
	if (m_pDevice3D) { m_pDevice3D->Release(); m_pDevice3D = nullptr; }
	if (m_pDirect3D) { m_pDirect3D->Release(); m_pDirect3D = nullptr; }

	::DestroyWindow(m_hAppWindow);
	::UnregisterClassW(L"WIN32WINDOWCLASS", m_hAppInstance);

	return true;
}

bool DXApp::InitMainWindow()
{
	//First step:
	//Create a window class structure to define our window
	WNDCLASSEX wcex;
	ZeroMemory(&wcex, sizeof(WNDCLASSEX)); //ZERO it out
	wcex.cbClsExtra = 0; //no extra bytes
	wcex.cbWndExtra = 0; //no extra bytes
	wcex.cbSize = sizeof(WNDCLASSEX); //set size in bytes
	wcex.style = CS_CLASSDC; //Basically states that window should be redraw both HORIZ. and VERT.
	wcex.hInstance = m_hAppInstance; //Set handle to application instance;
	wcex.lpfnWndProc = MainWndProc; //Set message procedure to our globally defined one
	wcex.hIcon = LoadIcon(NULL, IDI_APPLICATION); //Set window icon (standard application icon)
	wcex.hCursor = LoadCursor(NULL, IDC_ARROW); //Set window arrow (standard windows arrow)
	wcex.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH); //Set clear background
	wcex.lpszClassName = "WIN32WINDOWCLASS"; //Name it w.e you like. 
	wcex.lpszMenuName = NULL; //We are not using a menu at this time.
	wcex.hIconSm = LoadIcon(NULL, IDI_APPLICATION); //Set small window icon (standard application icon)

	//Now we must register the window class
	//Here is an example of some simple error checking
	//This can be quite useful for larger projects to debug errors
	if (!RegisterClassEx(&wcex))
	{
		MessageBox(NULL, "Failed to register window class", NULL, NULL);
		return false;
	}

	//Second step:
	//Cache the correct window dimensions
	RECT r = { 0, 0, m_ClientWidth, m_ClientHeight };
	AdjustWindowRect(&r, m_WindowStyle, false); //Use our window style
	int width = r.right - r.left;  //correct width based on requested client size
	int height = r.bottom - r.top;  //correct height based on requested client size
	int x;
	int y;
	if (testApp.selectedMonitorIndex == -1)
	{
		x = GetSystemMetrics(SM_CXSCREEN) / 2 - width / 2; //Centers window on desktop
		y = GetSystemMetrics(SM_CYSCREEN) / 2 - height / 2; //Centers window on desktop
	}
	else
	{

		x = testApp.monitors[testApp.selectedMonitorIndex].coordinates.left;
		y = testApp.monitors[testApp.selectedMonitorIndex].coordinates.top;

	}
	//Third step:
	//Create our window
	//lpClassName: MUST BE SAME AS ABOVE FROM WINDOW CLASS
	//lpWindowTitle: SHOULD BE m_AppTitle.c_str()
	m_hAppWindow = CreateWindow("WIN32WINDOWCLASS", m_AppTitle.c_str(), m_WindowStyle, x, y,
		width, height, NULL, NULL, m_hAppInstance, NULL);
	//Check window creation
	if (!m_hAppWindow)
	{
		MessageBox(NULL, "Failed to create esp window", NULL, NULL);
		return false;
	}

	//Fourth step:
	//Show window
	//SW_SHOW: Stand window display code, take the place of nCmdShow from entry point.
	ShowWindow(m_hAppWindow, SW_SHOW);

	//If all succeeded return true
	return true;
}

bool DXApp::InitDirect3D()
{
	//There are a few steps to successfully intializing a Direct3D device object
	//The first is to actually create the Direct3D interface object

	//Step 1:
	m_pDirect3D = Direct3DCreate9(D3D_SDK_VERSION);
	if (!m_pDirect3D)
	{
		MessageBox(NULL, "Failed to create Direct3D interface object", NULL, NULL);
		return false;
	}

	//Step 2:
	//We must check the display mode. The display mode defines
	//the pixel format our application is rendering to and only certain
	//screens have certain pixel formats supported. This is why we must
	//check if the formats we want are ok.

	//Cache the adapter display mode into our member variable
	m_pDirect3D->GetAdapterDisplayMode(D3DADAPTER_DEFAULT, &m_Mode);
	//Check both our WINDOWED and FULLSCREEN formats
	HR(m_pDirect3D->CheckDeviceType(D3DADAPTER_DEFAULT, m_DevType, m_Mode.Format, m_Mode.Format, true)); //WINDOWED
	//NOTE: D3DFMT_X8R8G8B8 is a widely supported display format. This is what we will be using.
	HR(m_pDirect3D->CheckDeviceType(D3DADAPTER_DEFAULT, m_DevType, D3DFMT_X8R8G8B8, D3DFMT_X8R8G8B8, false)); //FULLSCREEN

	//Step 3:
	//We must check to see if our graphics device supports hardware accelerated
	//vertex processing. Another term for this is HARDWARE TRANSFORM AND LIGHTING.
	//I'm going to assume virtually everyone following these tutorials has a device capable
	//of HWTRANSLIGHT
	int vp = 0;
	D3DCAPS9 devCaps;
	//Cache device caps
	HR(m_pDirect3D->GetDeviceCaps(D3DADAPTER_DEFAULT, m_DevType, &devCaps));
	if (devCaps.DevCaps & D3DDEVCAPS_HWTRANSFORMANDLIGHT)
		//Our device supports transformations in hardware
		vp = D3DCREATE_HARDWARE_VERTEXPROCESSING;
	else
		vp = D3DCREATE_SOFTWARE_VERTEXPROCESSING;

	//Step 4:
	//We must initialize our present parameters structure
	//This will tell our device how it should render to the back buffer, how many
	//buffers it has, etc. It is basically a swap chain description.
	ZeroMemory(&m_d3dpp, sizeof(D3DPRESENT_PARAMETERS));
	m_d3dpp.BackBufferWidth = GetSystemMetrics(SM_CXSCREEN);
	m_d3dpp.BackBufferHeight = GetSystemMetrics(SM_CYSCREEN);
	m_d3dpp.BackBufferFormat = D3DFMT_UNKNOWN; //32 bit format
	m_d3dpp.Windowed = true; //start windowed
	//m_d3dpp.BackBufferCount = 2; //Double buffered. 
	m_d3dpp.SwapEffect = D3DSWAPEFFECT_DISCARD;
	m_d3dpp.hDeviceWindow = m_hAppWindow;
	m_d3dpp.EnableAutoDepthStencil = true;
	m_d3dpp.AutoDepthStencilFormat = D3DFMT_D16;
	//m_d3dpp.PresentationInterval = D3DPRESENT_INTERVAL_ONE;           // Present with vsync
	m_d3dpp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
	//m_d3dpp.MultiSampleType = D3DMULTISAMPLE_NONE; // Adjust the sample count as needed
	//m_d3dpp.MultiSampleQuality = 0; // Quality level, usually set to 0

	//Step 5:
	//Create our device
	HR(m_pDirect3D->CreateDevice(D3DADAPTER_DEFAULT,
		D3DDEVTYPE_HAL, m_hAppWindow, D3DCREATE_HARDWARE_VERTEXPROCESSING, &m_d3dpp, &m_pDevice3D));

	// Step 2: Create a viewport matching the window size

	ZeroMemory(&viewport, sizeof(D3DVIEWPORT9));
	viewport.X = 0;
	viewport.Y = 0;
	viewport.Width = GetSystemMetrics(SM_CXSCREEN); // Use the current client width
	viewport.Height = GetSystemMetrics(SM_CYSCREEN); // Use the current client height
	viewport.MinZ = 0.0f;
	viewport.MaxZ = 1.0f;
	m_pDevice3D->SetViewport(&viewport);

	//set window position to selected monitor?


	if (globals::fuserFullscreen)
		EnableFullscreen(true);


	//If this all succeeds return true
	return true;
}

bool DXApp::IsDeviceLost()
{
	//Cache the state of the device
	HRESULT hr = m_pDevice3D->TestCooperativeLevel();
	if (hr == D3DERR_DEVICELOST) //If it is lost
	{
		//Sleep cpu for 1/10 of a second
		Sleep(100);
		return true;
	}
	else if (hr == D3DERR_DRIVERINTERNALERROR) //Fatal error occured
	{
		//Display message box
		MessageBox(NULL, "FATAL INTERNAL ERROR DETECTED.\n APPLICATION QUITTING", NULL, NULL);
		PostQuitMessage(0); //Quit application
		return true;
	}
	else if (hr == D3DERR_DEVICENOTRESET) //Device available for reset
	{
		//Destroy graphics
		OnLostDevice();

		//Reset the device
		HR(m_pDevice3D->Reset(&m_d3dpp));

		//Reset graphics
		OnResetDevice();

		//Device no longer lost
		return false;
	}
	else
		return false;
}

void DXApp::EndDraw()
{
	//pop render states
	for (auto a : m_RenderStates)
		m_pDevice3D->SetRenderState(a.dwState, a.dwValue);

	m_RenderStates.clear();

}

void DXApp::BeginDraw()
{
	//PushRenderState(D3DRS_COLORWRITEENABLE, 0xFFFFFFFF);
	//PushRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
	//PushRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
	//PushRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
	//PushRenderState(D3DRS_ZENABLE, D3DZB_FALSE);
	//PushRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
	//PushRenderState(D3DRS_FILLMODE, D3DFILL_SOLID);
	//PushRenderState(D3DRS_SHADEMODE, D3DSHADE_GOURAUD);

	PushRenderState(D3DRS_ZENABLE, D3DZB_FALSE);
	PushRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
	PushRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
	PushRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);

	m_pDevice3D->SetTexture(0, NULL);
	m_pDevice3D->SetPixelShader(NULL);

	m_pDevice3D->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
}

void DXApp::PushRenderState(const D3DRENDERSTATETYPE dwState, DWORD dwValue)
{
	DWORD dwTempValue;
	m_pDevice3D->GetRenderState(dwState, &dwTempValue);
	m_RenderStates.push_back({ dwState, dwTempValue });
	m_pDevice3D->SetRenderState(dwState, dwValue);
}

//Calculates frame per second
void DXApp::CalculateFPS(float dt)
{
	//Create static counters
	static int frameCnt;
	static float elapsedTime;

	//Increment counters
	frameCnt++;
	elapsedTime += dt;

	if (elapsedTime >= 1.0f)
	{
		m_FPS = (float)frameCnt;

		//std::stringstream ss;
		//ss << m_AppTitle.c_str() << "  FPS: " << m_FPS;
		//SetWindowText(m_hAppWindow, ss.str().c_str());

		//Reset counters
		frameCnt = 0;
		elapsedTime = 0;
	}
}

//Enables fullscreen mode
//This entails resetting the window style, window position,
//and redefining the present parameters back buffer.
void DXApp::EnableFullscreen(bool enable)
{
	if (enable)
	{
		//Cache desktop width and height
		int width = GetSystemMetrics(SM_CXSCREEN);
		int height = GetSystemMetrics(SM_CYSCREEN);

		m_d3dpp.BackBufferWidth = width;
		m_d3dpp.BackBufferHeight = height;
		m_d3dpp.BackBufferFormat = D3DFMT_X8R8G8B8;
		m_d3dpp.Windowed = true;

		//Set window style to fullscreen friendly
		SetWindowLongPtr(m_hAppWindow, GWL_STYLE, WS_POPUP);

		int x = testApp.monitors[testApp.selectedMonitorIndex].coordinates.left;
		int y = testApp.monitors[testApp.selectedMonitorIndex].coordinates.top;


		//Need to set new position for window
		SetWindowPos(m_hAppWindow, HWND_TOP, x, y, width, height, SWP_NOZORDER | SWP_SHOWWINDOW);
	}
	else
	{
		//Set window back to windowed mode
		RECT r = { 0, 0, m_ClientWidth, m_ClientHeight };
		AdjustWindowRect(&r, m_WindowStyle, false);
		int w = r.right - r.left;
		int h = r.bottom - r.top;
		m_d3dpp.BackBufferFormat = D3DFMT_X8R8G8B8;
		m_d3dpp.BackBufferWidth = GetSystemMetrics(SM_CXSCREEN);
		m_d3dpp.BackBufferHeight = GetSystemMetrics(SM_CYSCREEN);
		m_d3dpp.Windowed = true;

		//Change window style back to windowed friendly
		SetWindowLongPtr(m_hAppWindow, GWL_STYLE, m_WindowStyle);

		//Set window position
		SetWindowPos(m_hAppWindow, HWND_TOP,
			GetSystemMetrics(SM_CXSCREEN) / 2 - w / 2,
			GetSystemMetrics(SM_CYSCREEN) / 2 - h / 2,
			w, h, SWP_NOZORDER | SWP_SHOWWINDOW);
	}

	//Reset our device to reflect the changes
	OnLostDevice();
	//HR(m_pDevice3D->Reset(&m_d3dpp));
	OnResetDevice();
}

//Message procedure function.
//Windows OS runs of a message based system where all application
//that run on the OS send and receive messages that tell it what to do. To receive
//these messages our application needs what is called a message procedure function.
//Using this function we can "catch" these messages and tell out application what to do.
LRESULT DXApp::MsgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	//Switch statement on the message passed to us
	switch (msg)
	{
		//CASE: WM_DESTROY, our application is told to destroy itself
	case WM_DESTROY:
		espGlobals::runEsp = false;
		PostQuitMessage(0); //Tell the application to quit
		return 0;
	case WM_CLOSE:
		// Handle the close button click
		espGlobals::runEsp = false;
		CleanUp();
		PostQuitMessage(0); //Tell the application to quit
		return 0;
	case WM_SIZE:
		if (wParam == SIZE_MINIMIZED)
			return 0;
		// Queue resize
		g_requestedWidth = (UINT)LOWORD(lParam);
		g_requestedHeight = (UINT)HIWORD(lParam);
		HandleResize();
	case WM_KEYDOWN:
		// Check if the F1 key is pressed
		if (wParam == VK_F1)
		{

			m_EnableFullscreen = !m_EnableFullscreen;
			// Handle fullscreen mode
			EnableFullscreen(m_EnableFullscreen);
		}
		return 0;
	}

	//Always return the default window procedure if we don't catch anything
	return DefWindowProc(hwnd, msg, wParam, lParam);
}

void DXApp::HandleResize()
{
	if (m_pDevice3D)
	{
		D3DVIEWPORT9 viewport;
		ZeroMemory(&viewport, sizeof(D3DVIEWPORT9));
		viewport.X = 0;
		viewport.Y = 0;
		viewport.Width = GetSystemMetrics(SM_CXSCREEN); // Use the requested width
		viewport.Height = GetSystemMetrics(SM_CYSCREEN); // Use the requested height
		viewport.MinZ = 0.0f;
		viewport.MaxZ = 1.0f;
		m_pDevice3D->SetViewport(&viewport);

	}
}

bool DXApp::LoadFont()
{
	// Create the font object
	HRESULT hr = D3DXCreateFontA(m_pDevice3D, 18, 0, FW_NORMAL, 1, FALSE, DEFAULT_CHARSET,
		OUT_DEFAULT_PRECIS, DEFAULT_QUALITY, FW_NORMAL | FF_DONTCARE,
		"System", &g_pFont);

	if (FAILED(hr)) {
		// Handle font creation failure
		MessageBox(nullptr, "Failed to create font object!", "Error", MB_OK | MB_ICONERROR);
		return false;
	}
	else
	{
		return true;
	}
}

D3DXVECTOR2 DXApp::GetTextSize(ID3DXFont* pFont, const char* text)
{
	RECT rect = { 0, 0, 0, 0 }; // We're interested only in the right and bottom fields
	pFont->DrawTextA(NULL, text, -1, &rect, DT_CALCRECT, D3DCOLOR_XRGB(255, 255, 255)); // Calculate text size

	// Return the width and height of the calculated rectangle
	return D3DXVECTOR2(static_cast<float>(rect.right), static_cast<float>(rect.bottom));
}

void DXApp::RenderText(const char* text, int x, int y, D3DCOLOR color, bool center) {

	if (g_pFont) {

		RECT clientRect;
		GetClientRect(m_hAppWindow, &clientRect);


		RECT rect{};
		rect.left = x;
		rect.top = y;
		rect.right = clientRect.right - clientRect.left;
		rect.bottom = clientRect.bottom - clientRect.top;

		HRESULT hr = 0;
		if (!center)
			hr = g_pFont->DrawTextA(0, text, -1, &rect, DT_LEFT, color);
		else
		{
			D3DXVECTOR2 textSize = GetTextSize(g_pFont, text);
			rect.left -= (textSize.x / 2);
			hr = g_pFont->DrawTextA(0, text, -1, &rect, DT_LEFT, color);
		}


		if (FAILED(hr)) {
			// Handle the case when DrawText fails
			MessageBox(nullptr, "Failed to render text!", "Error", MB_OK | MB_ICONERROR);
		}
	}
	else {
		// Handle the case when the font object is null
		MessageBox(nullptr, "Font object is null!", "Error", MB_OK | MB_ICONERROR);
	}

}

void DXApp::DrawLine(int16_t x1, int16_t y1, int16_t x2, int16_t y2, color_t color, int16_t thickness)
{
	// Calculate the angle and length of the line
	float dx = static_cast<float>(x2 - x1);
	float dy = static_cast<float>(y2 - y1);
	float length = std::sqrt(dx * dx + dy * dy);

	// Normalize the direction vector
	if (length > 0)
	{
		dx /= length;
		dy /= length;
	}

	// Avoid long lines for bones
	if (length > 200)
		return;

	// Calculate the perpendicular vector
	float nx = -dy;
	float ny = dx;

	// Calculate the offset for the thickness
	float tx = nx * static_cast<float>(thickness) * 0.5f;
	float ty = ny * static_cast<float>(thickness) * 0.5f;

	// Define the vertices of the rectangle
	Vertex_t vertices[4] =
	{
		Vertex_t(x1 + tx, y1 + ty, color),
		Vertex_t(x1 - tx, y1 - ty, color),
		Vertex_t(x2 - tx, y2 - ty, color),
		Vertex_t(x2 + tx, y2 + ty, color)
	};

	// Draw the filled rectangle
	m_pDevice3D->DrawPrimitiveUP(D3DPT_TRIANGLEFAN, 2, vertices, sizeof(Vertex_t));
}

void DXApp::DrawFilledBox(int16_t x, int16_t y, int16_t width, int16_t height, color_t color)
{
	Vertex_t pVertex[4];
	pVertex[0] = Vertex_t(x, y, color);
	pVertex[1] = Vertex_t(x + width, y, color);
	pVertex[2] = Vertex_t(x, y + height, color);
	pVertex[3] = Vertex_t(x + width, y + height, color);

	m_pDevice3D->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, pVertex, sizeof(Vertex_t));
}

void DXApp::DrawBox(int16_t x, int16_t y, int16_t width, int16_t height, int16_t thickness, color_t color)
{
	DrawFilledBox(x, y, width, thickness, color);
	DrawFilledBox(x, y, thickness, height, color);
	DrawFilledBox(x + width - thickness, y, thickness, height, color);
	DrawFilledBox(x, y + height - thickness, width, thickness, color);
}

void DXApp::DrawBox(int16_t x, int16_t y, int16_t width, int16_t height, color_t color)
{
	Vertex_t pVertex[5];

	pVertex[0] = Vertex_t(x, y, color);
	pVertex[1] = Vertex_t(x + width, y, color);
	pVertex[2] = Vertex_t(x + width, y + height, color);
	pVertex[3] = Vertex_t(x, y + height, color);
	pVertex[4] = pVertex[0];

	m_pDevice3D->DrawPrimitiveUP(D3DPT_LINESTRIP, 4, pVertex, sizeof(Vertex_t));
}

void DXApp::DrawCornerBox(int16_t x, int16_t y, int16_t width, int16_t height, int16_t thickness, color_t color)
{
	int16_t cornerSize = height / 6;
	// Draw top-left corner box
	DrawFilledBox(x, y, thickness, cornerSize, color);
	DrawFilledBox(x, y, cornerSize, thickness, color);
	// Draw top-right corner box
	DrawFilledBox(x + width - thickness, y, thickness, cornerSize, color);
	DrawFilledBox(x + width - cornerSize, y, cornerSize, thickness, color);
	// Draw bottom-left corner box
	DrawFilledBox(x, y + height - thickness - cornerSize, thickness, cornerSize, color); // Adjusted y-coordinate
	DrawFilledBox(x, y + height - thickness, cornerSize, thickness, color); // Adjusted y-coordinate
	// Draw bottom-right corner box
	DrawFilledBox(x + width - cornerSize, y + height - thickness, cornerSize, thickness, color);
	DrawFilledBox(x + width - thickness, y + height - cornerSize, thickness, cornerSize, color);
}

//void DXApp::DrawCircle(int16_t x, int16_t y, int16_t radius, uint16_t points, float thickness, RenderDrawType flags, color_t color1, color_t color2)
//{
//	const bool gradient = (flags & RenderDrawType_Gradient);
//	const bool filled = (flags & RenderDrawType_Filled) || gradient;
//
//	if (filled) {
//		// Draw the filled circle
//		//DrawFilledCircle(x, y, radius, points, color1, flags);
//	}
//	else {
//		// Draw the outline circle
//		for (float r = radius - thickness; r <= radius; r += 0.5f) {
//			for (float angle = 0; angle < DirectX::XM_2PI; angle += DirectX::XM_2PI / points) {
//				float x1 = x + r * cos(angle);
//				float y1 = y + r * sin(angle);
//				float x2 = x + r * cos(angle + DirectX::XM_2PI / points);
//				float y2 = y + r * sin(angle + DirectX::XM_2PI / points);
//				DrawLine(x1, y1, x2, y2, color1);
//			}
//		}
//	}
//}

void DXApp::AddCircle(float centerX, float centerY, float radius, color_t color, int numSegments, float thickness)
{
	if (radius < 0.5f)
		return;

	if (numSegments <= 0)
	{
		// Use automatic segment count
		numSegments = 36; // Default segment count for automatic
	}
	else
	{
		// Clamp segment count to a reasonable range
		numSegments = std::clamp(numSegments, 3, 360); // Clamping to a range of 3 to 360 segments
	}

	float angleIncrement = 2.0f * D3DX_PI / static_cast<float>(numSegments);

	for (int i = 0; i < numSegments; ++i)
	{
		float angle1 = static_cast<float>(i) * angleIncrement;
		float angle2 = static_cast<float>(i + 1) * angleIncrement;

		float x1 = centerX + radius * std::cos(angle1);
		float y1 = centerY + radius * std::sin(angle1);

		float x2 = centerX + radius * std::cos(angle2);
		float y2 = centerY + radius * std::sin(angle2);

		DrawLine(x1, y1, x2, y2, color, thickness);
	}

	// Draw the final segment to close the circle
	DrawLine(centerX + radius * std::cos(0.0f), centerY + radius * std::sin(0.0f),
		centerX + radius * std::cos(2.0f * D3DX_PI), centerY + radius * std::sin(2.0f * D3DX_PI),
		color, thickness);
}

void DXApp::DrawBoneLine(glm::vec2 from, glm::vec2 to, color_t color, float thickness)
{

}
