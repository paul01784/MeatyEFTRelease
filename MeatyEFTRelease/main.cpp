#include "app/includes.h"
#include "app/globals.h"
#include "app/render.h"
#include "app/config.h"
#include "app/market.h"
#include "game/headers/maingame.h"
#include "game/headers/tarkovdevquery.h"
#include "app/DogTagAPI.h"



int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevIntance, PSTR pCmdLine, int nShowCmd)
{
	//Remove to get console debug
	::ShowWindow(::GetConsoleWindow(), SW_HIDE);

	LOGS.logInfo("[MAIN] Loading application");
	std::cout << "[MAIN] Loading application" << std::endl;

	//load configs
	ConfigManager configManager("config.json", "lootFilters.json");
	if (!configManager.LoadConfig()) {
		LOGS.logWarn("[MAIN][CONFIG] Failed to load config.json");

		if (!configManager.SaveConfig())
			LOGS.logWarn("[MAIN][CONFIG] Failed to save config.json");
	}
	if (!configManager.LoadLootFilterConfig()) {
		LOGS.logWarn("[MAIN][CONFIG] Failed to load lootFilters.json");
	}

	
	//set api key for dogtag api if we have one
	g_DogTagAPI.setApiKey(globals::dogTagAPIKey);

	//build exfil data
	TarkovDev tarkovDev;
	tarkovDev.Initialize();

	Sleep(1000);

	//start threads
	std::thread main(&MainGame::mainThread, &mainGame);
	main.detach();


	// launch main render screen
	while (true)
	{
		try
		{
			renderThread();
		}
		catch (...)
		{
			Sleep(1000);
		}
		break;
	}

	return 0;
}