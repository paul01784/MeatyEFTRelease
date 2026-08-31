#pragma once

#include <future>
#include <memory>
#include <mutex>
#include <string>

struct atlasVisibilityGlobals
{
    static bool enabled;
    static int maxDistance;
};

class AtlasVisibilityService
{
public:
    class CollisionMap;

    void visibilityTask();
    [[nodiscard]] std::string getStatusText() const;

private:
    void clearVisibilityFlags();
    void pollFactoryLoad();
    void startFactoryLoad();

    mutable std::mutex stateMutex;
    std::shared_ptr<const CollisionMap> factoryMap;
    std::future<std::shared_ptr<const CollisionMap>> factoryLoad;
    std::string statusText{ "Factory collision data is waiting" };
    bool factoryLoadStarted{ false };
};

extern AtlasVisibilityService atlasVisibility;
