#include "headers/exfil.h"

#include "../app/debug.h"
#include "headers/camera.h"
#include "../memory/Memory.h"
#include "headers/maingame.h"
#include "headers/utils.h"
#include "headers/unityHelper.h"
#include "headers/unitysdk.h"
#include "headers/transform.h"


Exfil exfil;

static std::string CleanString(std::string str)
{
	const size_t nullPos = str.find('\0');
	if (nullPos != std::string::npos)
		str.erase(nullPos);

	while (!str.empty() && (str.back() == ' ' || str.back() == '\r' || str.back() == '\n' || str.back() == '\t'))
		str.pop_back();

	return str;
}

static std::string ReadString(uint64_t namePtr)
{

	if (!Utils::valid_pointer(namePtr))
		return "";

	int len = mem.Read<int>(static_cast<SIZE_T>(namePtr) + 0x10);
	if (len <= 0 || len > 256)
		return "";

	return CleanString(mem.readUnicodeString(namePtr + 0x14, len));
}

void Exfil::exfilTask()
{
	try
	{
		if (!radarGlobals::drawExfils)
			return;

		//update exfil status on timer pass & local hands good
		if (!Utils::valid_pointer(mainGame.localPlayerHands))
			return;

		if (exfilList.size() < 1)
			tryLoadMemoryExfils();



		auto now = std::chrono::steady_clock::now();
		if (now - this->lastExfilStatusUpdate < std::chrono::seconds(4))
			return;
		this->lastExfilStatusUpdate = now;

		updateStatus();
	}
	catch (const std::exception& e) {
		LOGS.logError("Exception caught in exfilTask: " + std::string(e.what()) + ". Retrying...");
		return;
	}
	catch (...) {
		LOGS.logError("Unknown exception caught in exfilTask. Retrying...");
		return;
	}
}

void Exfil::clearCache()
{
	this->exfilList.clear();
}

std::vector<exfilsMemory>& Exfil::getCacheExfil() {
	return exfilList;
}

std::vector<exfilsSecret>& Exfil::getCacheSecret() {
	return exfilSecret;
}

std::vector<exfilsTransit>& Exfil::getCacheTransit() {
	return exfilTransit;
}

void Exfil::tryLoadMemoryExfils()
{
	if (!Utils::valid_pointer(mainGame.localGameWorld))
		return;

	try
	{

		uint64_t exfilController = mem.Read<uint64_t>(mainGame.localGameWorld + sdk::ClientLocalGameWorld::ExfiltrationController); if (!Utils::valid_pointer(exfilController)) return;

		uint64_t exfilOffset = 0x0;
		if (mainGame.localIsSavage)
			exfilOffset = sdk::ExfiltrationController::ScavExfiltrationPoints;
		else
			exfilOffset = sdk::ExfiltrationController::ExfiltrationPoints;

		uint64_t exfilArrayAddr = mem.Read<uint64_t>(exfilController + exfilOffset); if (!Utils::valid_pointer(exfilArrayAddr)) return;

		auto exfilArray = UnityArray<uint64_t>(exfilArrayAddr);

		for (auto& exfilPointAddr : exfilArray)
		{
			if (!Utils::valid_pointer(exfilPointAddr))
				continue;

			try 
			{

				uint64_t settingsAddr = mem.Read<uint64_t>(exfilPointAddr + sdk::ExfiltrationPoint::Settings);
				if (!Utils::valid_pointer(settingsAddr))
					continue;

				uint64_t namePtr = mem.Read<uint64_t>(settingsAddr + sdk::ExitSettings::Name);
				if (!Utils::valid_pointer(namePtr))
					continue;

				std::string exfilName = ReadString(namePtr);
				if (exfilName == "")
					continue;

				auto ti = mem.ReadChain(exfilPointAddr, TransformChain);
				if (!Utils::valid_pointer(ti))
					continue;
				auto transform = UnityTransform(ti);
				auto position = transform.UpdatePosition();

				exfilsMemory exfilNew;
				exfilNew.instance = exfilPointAddr;
				exfilNew.extractName = exfilName;
				exfilNew.locationWorld = position;

				exfilList.emplace_back(exfilNew);
				std::cout << "[EXFIL][MEM] Added exfil: '" + exfilName + "'\n";


			}
			catch (...)
			{
				//skip entry on error
			}
		}


	}
	catch (...)
	{

	}
}

std::string Exfil::getExfilStatusText(int statusInt)
{
	switch (statusInt)
	{
	case 1:
		return "Closed";
	case 2:
		return "Req";
	case 3:
		return "Countdown";
	case 4:
		return "Open";
	case 5:
		return "Pending";
	case 6:
		return "Await. Manual";

	}

}
int Exfil::getDistance(glm::vec3 point1, glm::vec3 point2)
{
	float dx = point1.x - point2.x;
	float dy = point1.y - point2.y;
	float dz = point1.z - point2.z;
	return std::sqrt(dx * dx + dy * dy + dz * dz);
}

void Exfil::updateStatus()
{
	if (exfilList.size() < 1)
		return;

	try 
	{
		auto handle = mem.CreateScatterHandle();

		for (auto& exfilCache : exfilList)
		{
			mem.AddScatterReadRequest(handle, exfilCache.instance + sdk::ExfiltrationPoint::Status, &exfilCache.statusRaw, sizeof(int));
		}

		mem.ExecuteReadScatter(handle); mem.CloseScatterHandle(handle);

		for (auto& exfilCache : exfilList)
		{
			exfilCache.status = getExfilStatusText(exfilCache.statusRaw);
			exfilCache.distance = getDistance(mainGame.localLocation, exfilCache.locationWorld);
		}



	}
	catch (...)
	{

	}

}

void Exfil::LoadEligibleEntryPoints(uint64_t exfilPointAddr)
{
	try
	{
		auto arrPtr = mem.Read<uint64_t>(exfilPointAddr + sdk::ExfiltrationPoint::EligibleEntryPoints);
		if (!Utils::valid_pointer(arrPtr))
			return;

		auto arr = UnityArray<uint64_t>(arrPtr);
		for (auto& strPtr : arr)
		{

			if (!Utils::valid_pointer(strPtr))
				continue;

			auto name = ReadString(strPtr);

			if (name != "")
				_pmcEntries.emplace_back(name);

		}

	}
	catch (...)
	{

	}

}

