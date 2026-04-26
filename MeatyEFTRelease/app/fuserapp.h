#pragma once

#include "../fuser/fUser.h"

struct MonitorInfo {
	std::string name;
	RECT coordinates;
};


class TestApp : public DXApp
{
public:
	//Constructor
	TestApp();
	//Destructor
	~TestApp();

	//Methods
	bool Init();
	void Update(float dt) override;
	void Render() override;
	void OnLostDevice() override;
	void OnResetDevice() override;

	void RenderLoot();

	void RenderNades(const D3DVIEWPORT9& viewport);
	void RenderTripWires(const D3DVIEWPORT9& viewport);
	void RenderPlayers(const D3DVIEWPORT9& viewport);
	void RenderHUD(const D3DVIEWPORT9& viewport);
	void RenderTasks(const D3DVIEWPORT9& viewport);

	void fuserMain();

	std::vector<MonitorInfo> EnumerateMonitors();

	static int fuser_fps;
	static std::vector<MonitorInfo> monitors;
	static int selectedMonitorIndex;

};

extern TestApp testApp;

