#include "headers/exfil.h"

#include "../app/debug.h"
#include "headers/camera.h"
#include "../memory/Memory.h"
#include "../memory/ScatterReadBatch.h"
#include "headers/maingame.h"
#include "headers/utils.h"
#include "headers/unityHelper.h"
#include "headers/unitysdk.h"
#include "headers/transform.h"

#include <algorithm>


Exfil::Exfil()
	: publishedExfilCache(
		std::make_shared<const ExfilCacheCollection>())
{
}

Exfil exfil;

void Exfil::exfilTask()
{
	try
	{
		if (!radarGlobals::drawExfils && !espGlobals::drawExfil)
			return;

		//update exfil status on timer pass & local hands good
		if (!Utils::valid_pointer(mainGame.localPlayerHands))
			return;

		auto now = std::chrono::steady_clock::now();
		constexpr auto kDiscoveryRetryInterval = std::chrono::seconds(8);

		// Discovery can lose a single entry when one dependent read is slow or
		// invalid.  Keep the existing cache, then periodically merge in anything
		// missed instead of treating a non-empty list as permanently complete.
		if (lastExfilDiscovery == std::chrono::steady_clock::time_point{} ||
			now - lastExfilDiscovery >= kDiscoveryRetryInterval)
		{
			lastExfilDiscovery = now;
			tryLoadMemoryExfils();
			publishCacheSnapshot();
		}

		if (now - this->lastExfilStatusUpdate < std::chrono::seconds(4))
			return;
		this->lastExfilStatusUpdate = now;

		updateStatus();
		publishCacheSnapshot();
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
	publishCacheSnapshot();
}

ExfilCacheSnapshot Exfil::getCacheExfilSnapshot() const noexcept {
	ExfilCacheSnapshot snapshot = publishedExfilCache.load(std::memory_order_acquire);

	if (snapshot)
		return snapshot;

	static const ExfilCacheSnapshot emptySnapshot = std::make_shared<const ExfilCacheCollection>();

	return emptySnapshot;
}

void Exfil::publishCacheSnapshot() {
	publishedExfilCache.store(
		std::make_shared<const ExfilCacheCollection>(exfilList),
		std::memory_order_release);
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

		auto exfilArray = UnityArray<uint64_t>(exfilArrayAddr, "Exfil points");

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

				std::string exfilName = TrimEFT(mem.readUnityString(namePtr, 256));
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

				const bool alreadyKnown = std::any_of(
					exfilList.begin(),
					exfilList.end(),
					[exfilPointAddr](const exfilsMemory& existing)
					{
						return existing.instance == exfilPointAddr;
					});

				if (alreadyKnown)
					continue;

				exfilList.emplace_back(std::move(exfilNew));
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
	default:
		return "Unknown";
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
		ScatterReadBatch batch(
			mem,
			true,
			"Exfil update"
		);

		if (!batch.Valid())
			return;

		for (auto& exfilCache : exfilList)
		{
			batch.Add(exfilCache.instance + sdk::ExfiltrationPoint::Status,exfilCache.statusRaw);
		}

		if (!batch.Execute())
			return;

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

		auto arr = UnityArray<uint64_t>(arrPtr, "Exfil requirements");
		for (auto& strPtr : arr)
		{

			if (!Utils::valid_pointer(strPtr))
				continue;

			auto name = TrimEFT(mem.readUnityString(strPtr, 256));

			if (name != "")
				_pmcEntries.emplace_back(name);

		}

	}
	catch (...)
	{

	}

}

