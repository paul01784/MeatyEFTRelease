/* Title: DirectX 9.0c Tutorial 03
/* Date: June 09, 2013
/* Description: Utility class for includes, defines, etc.
/* Youtube: http://www.youtube.com/user/Direct3DTutorials
/* Terms of Use: Free to be used in any project
/************************************************************************/

#pragma once

#define WIN32_LEAN_AND_MEAN  //strips away any nonessentials (i.e. winsockets, encryption, etc.)
//we only really care about the standard application stuff

#include <windows.h> //Include the windows header file, This contains all you will need to create a basic window
#include <string>	//needed for std::string
#include <sstream> //needed for std::stringstream (check CalculateFPS())
#include <d3d9.h> //needed for Direct3D

#include <d3dx9.h>
#include <dxerr.h>


//Link to necessary DIRECTX libraries
#pragma comment(lib, "d3d9.lib")
#pragma comment(lib, "d3dx9.lib")
#pragma comment(lib, "dxerr.lib") //used for our HR macro. DxTraceW is located here

//SAFE_RELEASE MACRO used to safely release a COM object
#define SAFE_RELEASE(x) { if(x) x->Release(); x=NULL; }
//SAFE_DELETE MACRO used to safely delete pointer objects from memory
#define SAFE_DELETE(x) { if(x) delete x; x = NULL; }

//D3DERR check MACRO, used to display message box containing
//line #, and error message from HRESULT returned by function call
#ifdef _DEBUG
#ifndef HR
#define HR(x)	 \
{			\
	HRESULT hr = x;	\
	if(FAILED(hr))	\
	{				\
		DXTraceW(__FILE__, __LINE__, hr, L#x, TRUE);	\
	}	\
}
#endif
#else
#ifndef HR
#define HR(x) x;
#endif
#endif // _DEBUG
