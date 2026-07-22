#include "app/includes.h"
#include "app/globals.h"
#include "app/render.h"
#include "app/config.h"
#include "game/headers/tarkovdevquery.h"
#include "game/headers/maingame.h"
#include "game/headers/tarkovdevquery.h"
#include "app/DogTagAPI.h"

#include "app/SplashWindow.h"
#include "app/resource.h"

#include <atomic>
#include <exception>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>

#include "app/FileUpdater.h"

// This shouldnt be changed unless your using your own manifest 
static constexpr const char* kManifestUrl = 
"https://www.dropbox.com/scl/fi/"
"mdn2wgndbvem5k2oc36xv/manifest.json"
"?rlkey=udsndylxalu2yov5mofhjzmjw"
"&st=55ltnlam"
"&dl=1";

// If you don't want to check files on load set to false !
static bool enableFileUpdater = true;

static std::filesystem::path GetApplicationDirectory()
{
    std::wstring path;
    path.resize(32768);

    const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));

    if (length == 0 || length >= path.size())
        throw std::runtime_error("Failed to obtain application directory");

    path.resize(length);

    return std::filesystem::path(path).parent_path();
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PSTR pCmdLine, int nShowCmd)
{
    ::ShowWindow(::GetConsoleWindow(), SW_HIDE);

    LOGS.logInfo("[MAIN] Loading application");

    SplashWindow splash;

    if (!splash.Create(hInstance, IDR_MEATY_LOGO, 520, 600))
    {
        std::wstring message = L"Failed to create the loading window.";

        if (!splash.GetLastErrorMessage().empty())
        {
            message += L"\n\n";
            message += splash.GetLastErrorMessage();
        }

        MessageBoxW(
            nullptr,
            message.c_str(),
            L"MeatyEFT",
            MB_OK | MB_ICONERROR
        );

        return 1;
    }

    std::atomic_bool startupFinished = false;
    std::atomic_bool startupSuccessful = false;

    std::exception_ptr startupException;

    std::mutex startupErrorMutex;
    std::wstring startupError;

    ConfigManager configManager("config.json", "lootFilters.json");

    TarkovDev tarkovDev;

    std::thread startupThread([&]()
        {
            try
            {
                const std::filesystem::path applicationDirectory = GetApplicationDirectory();

                splash.SetStatus(L"Checking application files...");

                if (enableFileUpdater)
                {
                    splash.SetStatus(L"Checking application files...");

                    FileUpdater updater;

                    const UpdateResult updateResult =
                        updater.Synchronise(
                            applicationDirectory,
                            kManifestUrl,
                            [&](const std::wstring& message)
                            {
                                splash.SetStatus(message);
                            }
                        );

                    for (const std::string& warning :
                        updateResult.warnings)
                    {
                        LOGS.logWarn("[MAIN][UPDATER] " + warning);
                    }

                    if (!updateResult.success)
                    {
                        throw std::runtime_error("File update check failed: " + updateResult.error);
                    }

                    LOGS.logInfo(
                        "[MAIN][UPDATER] Checked: " +
                        std::to_string(
                            updateResult.filesChecked
                        ) +
                        ", updated: " +
                        std::to_string(
                            updateResult.filesUpdated
                        ) +
                        ", skipped: " +
                        std::to_string(
                            updateResult.filesSkipped
                        ) +
                        ", failed: " +
                        std::to_string(
                            updateResult.filesFailed
                        )
                    );

                    if (updateResult.filesUpdated > 0)
                    {
                        splash.SetStatus(L"Updated " + std::to_wstring(updateResult.filesUpdated) + L" application file(s)");

                        Sleep(500);
                    }
                }
                else
                {
                    LOGS.logInfo("[MAIN][UPDATER] File updater disabled");

                    splash.SetStatus(L"Skipping application file check...");
                }

                splash.SetStatus(L"Loading configuration...");

                if (!configManager.LoadConfig())
                {
                    LOGS.logWarn("[MAIN][CONFIG] Failed to load config.json");

                    if (!configManager.SaveConfig())
                    {
                        LOGS.logWarn("[MAIN][CONFIG] Failed to save config.json");
                    }
                }

                if (!configManager.LoadLootFilterConfig())
                {
                    LOGS.logWarn("[MAIN][CONFIG] Failed to load lootFilters.json");
                }

                splash.SetStatus(L"Configuring services...");

                g_DogTagAPI.setApiKey(globals::dogTagAPIKey);

                splash.SetStatus(L"Loading Tarkov.dev data...");

                tarkovDev.Initialize();

                splash.SetStatus(L"Starting application threads...");

                std::thread mainGameThread(&MainGame::mainThread, &mainGame);

                mainGameThread.detach();

                splash.SetStatus(L"Ready");

                startupSuccessful.store(true, std::memory_order_release);
            }
            catch (const std::exception& exception)
            {
                LOGS.logError(std::string("[MAIN][STARTUP] ") + exception.what());

                {
                    std::lock_guard<std::mutex> lock(startupErrorMutex);

                    startupError.assign(exception.what(), exception.what() + std::strlen(exception.what()));
                }

                startupException = std::current_exception();
            }
            catch (...)
            {
                LOGS.logError("[MAIN][STARTUP] Unknown startup exception");

                {
                    std::lock_guard<std::mutex> lock(startupErrorMutex);

                    startupError = L"An unknown startup error occurred.";
                }

                startupException = std::current_exception();
            }

            startupFinished.store(true, std::memory_order_release);
        });

    while (!startupFinished.load(std::memory_order_acquire))
    {
        if (!splash.PumpMessages())
            break;

        Sleep(10);
    }

    if (startupThread.joinable())
        startupThread.join();

    if (!startupSuccessful.load(std::memory_order_acquire))
    {
        splash.SetStatus(L"Application initialisation failed"
        );

        const DWORD failureStart = GetTickCount();

        while (GetTickCount() - failureStart < 1000)
        {
            splash.PumpMessages();
            Sleep(10);
        }

        splash.FadeOut();
        splash.Close();

        std::wstring error;

        {
            std::lock_guard<std::mutex> lock(startupErrorMutex);

            error = startupError;
        }

        if (error.empty())
            error = L"Application initialisation failed.";

        MessageBoxW(
            nullptr,
            error.c_str(),
            L"MeatyEFT startup error",
            MB_OK | MB_ICONERROR
        );

        return 1;
    }

    const DWORD readyStart = GetTickCount();

    while (GetTickCount() - readyStart < 300)
    {
        splash.PumpMessages();
        Sleep(10);
    }

    splash.FadeOut();
    splash.Close();

    for (;;)
    {
        try
        {
            renderThread();
            break;
        }
        catch (const std::exception& exception)
        {
            LOGS.logError(std::string("[MAIN][RENDER] ") + exception.what());

            Sleep(1000);
        }
        catch (...)
        {
            LOGS.logError("[MAIN][RENDER] Unknown rendering exception");

            Sleep(1000);
        }
    }

    return 0;
}