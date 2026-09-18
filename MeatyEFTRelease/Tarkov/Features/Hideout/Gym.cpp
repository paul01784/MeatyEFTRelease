#include "Gym.h"

#include "../../SDK/EftOffsets.h"
#include "../../Unity/UnityOffsets.h"
#include "../../Unity/UnityContainers.h"
#include "../../Unity/Transform.h"
#include "../../../Core/Utilities.h"
#include "../../../memory/Memory.h"
#include "../../../memory/ScatterReadBatch.h"
#include "../../../Core/InputDevice.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <limits>
#include <unordered_map>
#include <unordered_set>
#include <vector>

Gym GYM;

namespace
{
    constexpr DmaCacheMode kGymReadMode = DmaCacheMode::Uncached;
    constexpr auto kHideoutScanInterval = std::chrono::seconds(1);
    constexpr std::uint64_t kEftComponentObjectClassOffset = 0x20;
    constexpr auto kTransformResolveInterval = std::chrono::milliseconds(100);
    constexpr std::int32_t kDirectQteChunkObjects = 24;
    constexpr std::int32_t kMaxTransformIndex = 1'000'000;
    constexpr std::uint64_t kIl2CppClassStaticFields = 0xB8;

#pragma pack(push, 8)
    // Keep the EFT/Unity active-object list layout that was working before
    // the UnityOffsets.h migration. The generated header currently labels
    // the two link directions in the opposite order for this traversal.
    struct GymObjectListNode
    {
        std::uint64_t previous{}; // +0x00
        std::uint64_t next{};     // +0x08
        std::uint64_t object{};   // +0x10
    };
#pragma pack(pop)

    struct GymComponentArray
    {
        std::uint64_t entries{};
        std::uint64_t memoryLabel{};
        std::uint64_t size{};
        std::uint64_t capacity{};
    };

    struct GymComponentEntry
    {
        std::uint64_t unknown0{};
        std::uint64_t component{};
    };

    static_assert(offsetof(GymObjectListNode, previous) == 0x00);
    static_assert(offsetof(GymObjectListNode, next) == 0x08);
    static_assert(offsetof(GymObjectListNode, object) == 0x10);
    static_assert(offsetof(GymComponentArray, size) == UnityOffsets::ComponentArray_SizeOffset);
    static_assert(offsetof(GymComponentArray, capacity) == UnityOffsets::ComponentArray_CapacityOffset);
    static_assert(offsetof(GymComponentEntry, component) == 0x8);
    static_assert(sizeof(GymComponentEntry) == 0x10);

#pragma pack(push, 1)
    struct GymCirclePrimaryFields
    {
        float speed{};
        GymVec2 successRange{};
        float minScale{};
        std::int32_t targetInput{};
        std::uint32_t padding{};
        std::uint64_t dynamicImage{};
        std::uint64_t dynamicInnerBorder{};
        std::uint64_t dynamicOuterBorder{};
    };

    struct GymCircleResultFields
    {
        double successStartScale{};
        double successEndScale{};
        bool isSuccess{};
        std::uint8_t padding[7]{};
    };
#pragma pack(pop)

    static_assert(offsetof(GymCirclePrimaryFields, successRange) == sdk::ShrinkingCircleQTE::SuccessRange - sdk::ShrinkingCircleQTE::Speed);
    static_assert(offsetof(GymCirclePrimaryFields, minScale) == sdk::ShrinkingCircleQTE::MinScale - sdk::ShrinkingCircleQTE::Speed);
    static_assert(offsetof(GymCirclePrimaryFields, targetInput) == sdk::ShrinkingCircleQTE::TargetInput - sdk::ShrinkingCircleQTE::Speed);
    static_assert(offsetof(GymCirclePrimaryFields, dynamicImage) == sdk::ShrinkingCircleQTE::DynamicCircleImage - sdk::ShrinkingCircleQTE::Speed);
    static_assert(offsetof(GymCirclePrimaryFields, dynamicInnerBorder) == sdk::ShrinkingCircleQTE::DynamicCircleInnerBorder - sdk::ShrinkingCircleQTE::Speed);
    static_assert(offsetof(GymCircleResultFields, successEndScale) == sdk::ShrinkingCircleQTE::SuccessEndScale - sdk::ShrinkingCircleQTE::SuccessStartScale);
    static_assert(offsetof(GymCircleResultFields, isSuccess) == sdk::ShrinkingCircleQTE::IsSuccess - sdk::ShrinkingCircleQTE::SuccessStartScale);
}

Gym::~Gym()
{
    Stop();
}

void Gym::Configure(bool enabled, bool autoClick, bool inRaid, bool dmaReady)
{
    inRaid_.store(inRaid, std::memory_order_release);
    dmaReady_.store(dmaReady, std::memory_order_release);
    autoClickEnabled_.store(autoClick, std::memory_order_release);
    enabled_.store(enabled, std::memory_order_release);

    const bool active = enabled && !inRaid && dmaReady;
    const bool wasActive = runtimeActive_.exchange(active, std::memory_order_acq_rel);
    if (active != wasActive)
    {
        generation_.fetch_add(1, std::memory_order_acq_rel);
        std::lock_guard<std::mutex> snapshotLock(snapshotMutex_);
        snapshot_ = {};
    }

    if (active)
    {
        std::lock_guard<std::mutex> workerLock(workerMutex_);
        if (!worker_.joinable())
            worker_ = std::jthread([this](std::stop_token stopToken) { WorkerLoop(stopToken); });
    }
}

void Gym::Stop()
{
    std::jthread worker;
    {
        std::lock_guard<std::mutex> workerLock(workerMutex_);
        if (!worker_.joinable())
            return;
        worker = std::move(worker_);
    }

    worker.request_stop();
}

void Gym::WorkerLoop(std::stop_token stopToken)
{
    std::uint64_t workerGeneration = (std::numeric_limits<std::uint64_t>::max)();

    while (!stopToken.stop_requested())
    {
        const std::uint64_t generation = generation_.load(std::memory_order_acquire);
        if (!ShouldRead(generation))
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        if (workerGeneration != generation)
        {
            ResetWorkerState();
            workerGeneration = generation;
        }

        // Keep the original resolver/discovery path unchanged. Only increase
        // cadence after Update() has actually found a live ShrinkingCircleQTE.
        const bool liveQte = Update();
        const int sleepMilliseconds = liveQte ? 5 : 20;

        for (int elapsed = 0; elapsed < sleepMilliseconds && !stopToken.stop_requested(); elapsed += 5)
        {
            if (!ShouldRead(generation))
                break;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }
}

bool Gym::Update()
{
    const std::uint64_t generation = generation_.load(std::memory_order_acquire);
    GymSnapshot state{};

    const auto finish = [&](bool active)
    {
        state.autoClickEnabled = autoClickEnabled_.load(std::memory_order_acquire);
        state.inputDeviceConnected = inputDevice.IsConnected();
        state.autoClickCount = autoClickCount_;
        state.lastAutoClickSucceeded = lastAutoClickSucceeded_;
        state.lastAutoClickEvent = lastAutoClickEvent_;

        // These are presentation-only readiness flags. The original resolver
        // logic above continues to use the real controller/static-field state.
        state.staticFieldsResolved = state.staticFieldsValid;
        state.controllerSpawned = state.controllerValid;
        state.resolverReady = state.hideoutAreaValid &&
                              state.typeInfoValid &&
                              state.overlayClassValid &&
                              state.qteControllerClassValid &&
                              state.shrinkingCircleClassValid;
        state.readyToStart = state.resolverReady && state.inputDeviceConnected && !state.shrinkingCircleValid;

        // _spawnedQtes is transient in EFT. Keep the last observed address in
        // the published snapshot so Debug does not look as if our code clears
        // it every frame. This does NOT feed back into resolver logic because
        // every Update() starts from a fresh local GymSnapshot.
        if (Utils::valid_pointer(state.spawnedQtes))
            lastPublishedSpawnedQtes_ = state.spawnedQtes;
        else if (Utils::valid_pointer(lastPublishedSpawnedQtes_))
            state.spawnedQtes = lastPublishedSpawnedQtes_;

        // Compatibility with the existing AutoPress Status renderer. It used
        // these legacy fields as the overall resolver-ready test. Only alter
        // them here, after all resolver decisions for this pass are complete.
        if (state.readyToStart)
        {
            state.staticFieldsValid = true;
            state.controllerValid = true;
        }

        if (!state.autoClickEnabled)
        {
            state.autoClickStatus = "Disabled";
        }
        else if (state.autoClickStatus.empty())
        {
            if (!state.inputDeviceConnected)
                state.autoClickStatus = "Waiting for input-device connection";
            else if (state.readyToStart)
                state.autoClickStatus = "Ready - start the gym game";
            else if (!state.resolverReady && !state.shrinkingCircleValid)
                state.autoClickStatus = "Preparing Gym AutoPress";
            else if (!state.shrinkingCircleValid)
                state.autoClickStatus = "Waiting for live QTE";
            else if (!state.circleScaleValid)
                state.autoClickStatus = "Waiting for valid circle scale";
            else
                state.autoClickStatus = "Waiting for success window";
        }

        if (!state.shrinkingCircleValid)
        {
            lastObservedCircle_ = 0;
            lastPredictedSuccess_ = false;
        }

        PublishSnapshot(std::move(state), generation);
        return active;
    };

    if (!ShouldRead(generation))
        return finish(false);

    // Hideout discovery is diagnostic context, not a prerequisite for the
    // supplied TypeInfo/QTE chain. This keeps the useful resolver stages live
    // if a game update renames the Hideout behaviour.
    (void)ResolveHideoutArea(state, generation);

    if (!ShouldRead(generation))
        return finish(false);

    const std::uint64_t typeInfoTable = ResolveTypeInfoTable();
    state.typeInfoTable = typeInfoTable;
    state.typeInfoValid = Utils::valid_pointer(typeInfoTable);

    if (!state.typeInfoValid)
        return finish(false);

    const std::uint64_t overlayClass = ResolveClass(typeInfoTable, sdk::HideoutAreaQTEOverlay::TypeIndex);
    const std::uint64_t qteControllerClass = ResolveClass(typeInfoTable, sdk::QTEController::TypeIndex);
    const std::uint64_t shrinkingCircleClass = ResolveClass(typeInfoTable, sdk::ShrinkingCircleQTE::TypeIndex);

    if (!ShouldRead(generation))
        return finish(false);

    state.overlayClass = overlayClass;
    state.overlayClassValid = Utils::valid_pointer(overlayClass);
    state.qteControllerClass = qteControllerClass;
    state.qteControllerClassValid = Utils::valid_pointer(qteControllerClass);
    state.shrinkingCircleClass = shrinkingCircleClass;
    state.shrinkingCircleClassValid = Utils::valid_pointer(shrinkingCircleClass);
    state.directObjectScanAttempted = lastDirectQteScan_ != std::chrono::steady_clock::time_point{};
    state.directObjectScanCount = lastDirectQteObjectCount_;

    std::uint64_t shrinkingCircle = 0;
    if (state.overlayClassValid && state.qteControllerClassValid)
    {
        std::uint64_t staticFieldsOffset = 0;
        const std::uint64_t staticFields = ResolveOverlayStaticFields(overlayClass, qteControllerClass, staticFieldsOffset);

        if (!ShouldRead(generation))
            return finish(false);

        state.staticFields = staticFields;
        state.staticFieldsOffset = staticFieldsOffset;
        state.staticFieldsValid = Utils::valid_pointer(staticFields);

        if (state.staticFieldsValid)
        {
            (void)mem.TryRead(staticFields + sdk::HideoutAreaQTEOverlay::QteController, state.qteController, kGymReadMode);
            state.controllerValid = IsObjectOfClass(state.qteController, qteControllerClass);
        }

        if (state.controllerValid && ShouldRead(generation))
        {
            state.spawnedListValid = ReadSpawnedList(state.qteController, state.spawnedQtes, state.spawnedCount);
            if (state.spawnedListValid && ShouldRead(generation))
            {
                bool matchedByName = false;
                shrinkingCircle = FindShrinkingCircle(state.spawnedQtes, state.spawnedCount, shrinkingCircleClass, state.spawnedClassNames, matchedByName);
                if (Utils::valid_pointer(shrinkingCircle))
                    state.circleResolver = matchedByName ? "spawnedQtes (class-name validation)" : "spawnedQtes (type-index class)";
            }
        }
    }

    // A valid spawned list is authoritative. Do not keep presenting the last
    // directly-crawled QTE after the controller has removed it.
    if (state.spawnedListValid && state.spawnedCount == 0)
    {
        cachedDirectShrinkingCircle_ = 0;
        ResetCircleTransformCache();
    }

    if (!Utils::valid_pointer(shrinkingCircle) && Utils::valid_pointer(cachedDirectShrinkingCircle_) &&
        (IsObjectOfClass(cachedDirectShrinkingCircle_, shrinkingCircleClass) ||
         _stricmp(ReadBehaviourClassName(cachedDirectShrinkingCircle_).c_str(), "ShrinkingCircleQTE") == 0))
    {
        shrinkingCircle = cachedDirectShrinkingCircle_;
        state.circleResolvedByObjectScan = true;
        state.circleResolver = "Unity active-object cache";
    }

    const auto now = std::chrono::steady_clock::now();
    if (!Utils::valid_pointer(shrinkingCircle) && state.hideoutAreaValid && state.gameObjectManagerValid)
    {
        std::uint64_t newestNode = 0;
        (void)mem.TryRead(state.gameObjectManager + UnityOffsets::GameObjectManager_LastActiveNodeOffset, newestNode, kGymReadMode);
        if (Utils::valid_pointer(newestNode))
        {
            if (newestNode != lastDirectTailNode_)
            {
                directQteStopNode_ = lastDirectTailNode_;
                lastDirectTailNode_ = newestNode;
                directQteScanCursor_ = newestNode;
                lastDirectQteObjectCount_ = 0;
                lastDirectQteScan_ = now;
            }

            if (Utils::valid_pointer(directQteScanCursor_) && lastDirectQteObjectCount_ < kMaxDirectQteObjects)
            {
                std::int32_t chunkCount = 0;
                std::uint64_t nextNode = 0;
                shrinkingCircle = FindShrinkingCircleBehaviour(directQteScanCursor_, shrinkingCircleClass, chunkCount, nextNode, directQteStopNode_, generation);
                directQteScanCursor_ = nextNode;
                lastDirectQteObjectCount_ += chunkCount;
                if (directQteScanCursor_ == directQteStopNode_ || lastDirectQteObjectCount_ >= kMaxDirectQteObjects)
                    directQteScanCursor_ = 0;
                state.directObjectScanAttempted = true;
                state.directObjectScanCount = lastDirectQteObjectCount_;
                if (Utils::valid_pointer(shrinkingCircle))
                {
                    cachedDirectShrinkingCircle_ = shrinkingCircle;
                    state.circleResolvedByObjectScan = true;
                    state.circleResolver = "Unity newest-object scan";
                }
            }
        }
    }

    state.shrinkingCircle = shrinkingCircle;
    state.shrinkingCircleValid = Utils::valid_pointer(shrinkingCircle);

    if (!state.shrinkingCircleValid)
        return finish(false);

    GymCirclePrimaryFields primary{};
    GymCircleResultFields result{};
    ScatterReadBatch circleRead(mem, kGymReadMode, "Gym live circle");
    const bool queuedPrimary = circleRead.Add(shrinkingCircle + sdk::ShrinkingCircleQTE::Speed, primary);
    const bool queuedResult = circleRead.Add(shrinkingCircle + sdk::ShrinkingCircleQTE::SuccessStartScale, result);
    if (queuedPrimary && queuedResult && circleRead.Execute())
    {
        state.speed = primary.speed;
        state.successRange = primary.successRange;
        state.minScale = primary.minScale;
        state.targetInput = primary.targetInput;
        state.dynamicCircleImage = primary.dynamicImage;
        state.dynamicCircleInnerBorder = primary.dynamicInnerBorder;
        state.successStartScale = result.successStartScale;
        state.successEndScale = result.successEndScale;
        state.isSuccess = result.isSuccess;
    }

    (void)ResolveCircleTransform(shrinkingCircle, state.dynamicCircleInnerBorder, state.dynamicCircleImage, state);
    if (state.transformAccessValid && ReadCachedCircleScale(state.circleScale))
    {
        const float scale = state.circleScale.x;
        state.circleScaleValid = std::isfinite(scale) && std::isfinite(state.circleScale.y) &&
                                 std::isfinite(state.circleScale.z) && scale > 0.0f && state.circleScale.y > 0.0f;

        float successMinimum = std::min(state.successRange.x, state.successRange.y);
        float successMaximum = std::max(state.successRange.x, state.successRange.y);

        const double calculatedMinimum = std::min(state.successStartScale, state.successEndScale);
        const double calculatedMaximum = std::max(state.successStartScale, state.successEndScale);
        const bool calculatedRangeValid = std::isfinite(calculatedMinimum) && std::isfinite(calculatedMaximum) &&
                                          calculatedMinimum > 0.0 && calculatedMaximum > calculatedMinimum &&
                                          calculatedMaximum < 10.0;
        if (calculatedRangeValid)
        {
            successMinimum = static_cast<float>(calculatedMinimum);
            successMaximum = static_cast<float>(calculatedMaximum);
            state.calculatedSuccessWindow = true;
        }

        const bool successRangeValid = std::isfinite(successMinimum) && std::isfinite(successMaximum) &&
                                       successMinimum > 0.0f && successMaximum > successMinimum;
        state.successWindowMinimum = successMinimum;
        state.successWindowMaximum = successMaximum;
        state.predictedSuccess = state.circleScaleValid && successRangeValid &&
                                 scale >= successMinimum && scale <= successMaximum;

        if (!successRangeValid)
            state.autoClickStatus = "Invalid success window";
        else if (!state.predictedSuccess)
            state.autoClickStatus = "Waiting for success window";
    }
    else if (state.transformAccessValid)
    {
        cachedTransformAccess_ = 0;
        cachedTransformData_ = 0;
        cachedTransformVertices_ = 0;
        cachedTransformIndex_ = -1;
        cachedScaleResolver_.clear();
        state.transformAccessValid = false;
    }

    if (shrinkingCircle != lastObservedCircle_)
    {
        lastObservedCircle_ = shrinkingCircle;
        lastPredictedSuccess_ = false;
    }

    const bool autoClick = autoClickEnabled_.load(std::memory_order_acquire);
    const bool successRisingEdge = state.predictedSuccess && !lastPredictedSuccess_;

    if (autoClick && state.predictedSuccess && !inputDevice.IsConnected())
    {
        state.autoClickStatus = "Success window reached; input device disconnected";
    }
    else if (autoClick && successRisingEdge && ShouldRead(generation) && inputDevice.IsConnected())
    {
        state.autoClickStatus = "Sending input-device left click";
        const std::string deviceName = inputDevice.GetConnectedName();
        lastAutoClickSucceeded_ = inputDevice.Click(InputMouseButton::Left, 8, 25);
        if (lastAutoClickSucceeded_)
        {
            ++autoClickCount_;
            lastAutoClickEvent_ = "Left click sent at scale " + std::to_string(state.circleScale.x);
            state.autoClickStatus = deviceName + " left click sent";
        }
        else
        {
            lastAutoClickEvent_ = deviceName + " left click failed";
            state.autoClickStatus = lastAutoClickEvent_;
        }
        lastPredictedSuccess_ = true;
    }
    else if (autoClick && state.predictedSuccess)
    {
        state.autoClickStatus = "Success window already handled";
    }
    else if (!autoClick || !state.predictedSuccess)
    {
        lastPredictedSuccess_ = false;
    }

    return finish(true);
}

void Gym::Reset()
{
    generation_.fetch_add(1, std::memory_order_acq_rel);
    std::lock_guard<std::mutex> snapshotLock(snapshotMutex_);
    snapshot_ = {};
}

void Gym::ResetWorkerState()
{
    lastHideoutScan_ = {};
    lastObjectListValid_ = false;
    lastObjectCount_ = 0;
    lastGameObjectManager_ = 0;
    cachedHideoutArea_ = 0;
    cachedHideoutRuntimeClass_ = 0;
    cachedHideoutClassName_.clear();
    lastObservedCircle_ = 0;
    cachedDirectShrinkingCircle_ = 0;
    ResetCircleTransformCache();
    lastDirectTailNode_ = 0;
    directQteScanCursor_ = 0;
    directQteStopNode_ = 0;
    autoClickCount_ = 0;
    lastDirectQteScan_ = {};
    lastDirectQteObjectCount_ = 0;
    lastPublishedSpawnedQtes_ = 0;
    lastPredictedSuccess_ = false;
    lastAutoClickSucceeded_ = false;
    lastAutoClickEvent_.clear();
}

bool Gym::IsActive() const
{
    return GetSnapshot().shrinkingCircleValid;
}

GymSnapshot Gym::GetSnapshot() const
{
    std::lock_guard<std::mutex> snapshotLock(snapshotMutex_);
    return snapshot_;
}

bool Gym::ShouldRead(std::uint64_t generation) const
{
    return generation_.load(std::memory_order_acquire) == generation &&
           runtimeActive_.load(std::memory_order_acquire) &&
           enabled_.load(std::memory_order_acquire) &&
           !inRaid_.load(std::memory_order_acquire) &&
           dmaReady_.load(std::memory_order_acquire);
}

void Gym::PublishSnapshot(GymSnapshot snapshot, std::uint64_t generation)
{
    if (!ShouldRead(generation))
        return;

    std::lock_guard<std::mutex> snapshotLock(snapshotMutex_);
    if (ShouldRead(generation))
        snapshot_ = std::move(snapshot);
}

bool Gym::ResolveHideoutArea(GymSnapshot& state, std::uint64_t generation)
{
    const TarkovPointerSnapshot pointers = mem.GetTarkovPointerSnapshot();
    std::uint64_t gom = pointers.gameObjectManager;

    if (!Utils::valid_pointer(gom) && Utils::valid_pointer(pointers.unityPlayerBase))
        (void)mem.TryRead(pointers.unityPlayerBase + UnityOffsets::GameObjectManager, gom, kGymReadMode);

    std::uint64_t cachedRuntimeClass = 0;
    if (Utils::valid_pointer(cachedHideoutArea_) && Utils::valid_pointer(cachedHideoutRuntimeClass_) &&
        mem.TryRead(cachedHideoutArea_, cachedRuntimeClass, kGymReadMode) && cachedRuntimeClass == cachedHideoutRuntimeClass_)
    {
        state.hideoutScanAttempted = true;
        state.gameObjectManager = gom;
        state.gameObjectManagerValid = Utils::valid_pointer(gom);
        state.objectListValid = lastObjectListValid_;
        state.objectCount = lastObjectCount_;
        state.hideoutArea = cachedHideoutArea_;
        state.hideoutAreaValid = true;
        state.hideoutClassName = cachedHideoutClassName_;
        return true;
    }

    cachedHideoutArea_ = 0;
    cachedHideoutRuntimeClass_ = 0;
    cachedHideoutClassName_.clear();

    const auto now = std::chrono::steady_clock::now();
    if (lastHideoutScan_ != std::chrono::steady_clock::time_point{} && now - lastHideoutScan_ < kHideoutScanInterval)
    {
        PublishLastHideoutScan(state);
        return false;
    }
    lastHideoutScan_ = now;

    lastGameObjectManager_ = gom;
    lastObjectListValid_ = false;
    lastObjectCount_ = 0;

    state.hideoutScanAttempted = true;
    state.gameObjectManager = gom;
    state.gameObjectManagerValid = Utils::valid_pointer(gom);

    if (!state.gameObjectManagerValid || !ShouldRead(generation))
        return false;

    PublishSnapshot(state, generation);

    cachedHideoutArea_ = FindHideoutBehaviour(gom, cachedHideoutClassName_, generation);
    if (Utils::valid_pointer(cachedHideoutArea_))
        (void)mem.TryRead(cachedHideoutArea_, cachedHideoutRuntimeClass_, kGymReadMode);
    state.objectListValid = lastObjectListValid_;
    state.objectCount = lastObjectCount_;
    state.hideoutArea = cachedHideoutArea_;
    state.hideoutAreaValid = Utils::valid_pointer(cachedHideoutArea_);
    state.hideoutClassName = cachedHideoutClassName_;
    return state.hideoutAreaValid;
}

std::uint64_t Gym::FindHideoutBehaviour(std::uint64_t gom, std::string& matchedClass, std::uint64_t generation)
{
    matchedClass.clear();

    if (!Utils::valid_pointer(gom) || !ShouldRead(generation))
        return 0;

    std::uint64_t activeNode = 0;
    std::uint64_t lastNode = 0;
    if (!mem.TryRead(gom + UnityOffsets::GameObjectManager_ActiveNodesOffset, activeNode, kGymReadMode) ||
        !mem.TryRead(gom + UnityOffsets::GameObjectManager_LastActiveNodeOffset, lastNode, kGymReadMode) ||
        !Utils::valid_pointer(activeNode) || !Utils::valid_pointer(lastNode))
    {
        return 0;
    }

    lastObjectListValid_ = true;

    GymSnapshot progress{};
    PublishLastHideoutScan(progress);
    PublishSnapshot(progress, generation);

    std::unordered_set<std::uint64_t> visited;
    visited.reserve(1024);
    std::unordered_map<std::uint64_t, std::string> classNames;
    classNames.reserve(256);

    const auto inspectObject = [&](std::uint64_t object) -> std::uint64_t
    {
        if (!Utils::valid_pointer(object))
            return 0;

        GymComponentArray components{};
        if (!mem.TryRead(object + UnityOffsets::GameObject_ComponentsOffset, components, kGymReadMode) ||
            !Utils::valid_pointer(components.entries) || components.size == 0 || components.size > 0x400)
        {
            return 0;
        }

        std::vector<GymComponentEntry> entries(static_cast<std::size_t>(components.size));
        if (!mem.Read(components.entries, entries.data(), entries.size() * sizeof(GymComponentEntry), kGymReadMode, "Gym Hideout components"))
            return 0;

        for (const GymComponentEntry& entry : entries)
        {
            if (!ShouldRead(generation) || !Utils::valid_pointer(entry.component))
                continue;

            std::uint64_t behaviour = 0;
            std::uint64_t klass = 0;
            if (!mem.TryRead(entry.component + kEftComponentObjectClassOffset, behaviour, kGymReadMode) || !Utils::valid_pointer(behaviour) ||
                !mem.TryRead(behaviour, klass, kGymReadMode) || !Utils::valid_pointer(klass))
            {
                continue;
            }

            auto classNameIt = classNames.find(klass);
            if (classNameIt == classNames.end())
                classNameIt = classNames.emplace(klass, ReadBehaviourClassName(behaviour)).first;

            const std::string& className = classNameIt->second;
            if (_stricmp(className.c_str(), "HideoutArea") == 0 ||
                _stricmp(className.c_str(), "HideoutController") == 0 ||
                _stricmp(className.c_str(), "HideoutAreaQTEOverlay") == 0)
            {
                matchedClass = className;
                return behaviour;
            }
        }

        return 0;
    };

    // Hideout objects are created late and tend to be close to LastActiveNode.
    // Inspect while walking so finding a recent object does not wait for the
    // entire active list to be buffered first.
    bool reachedActiveNode = false;
    std::uint64_t current = lastNode;
    while (ShouldRead(generation) && Utils::valid_pointer(current) && lastObjectCount_ < kMaxGameObjects && visited.emplace(current).second)
    {
        GymObjectListNode node{};
        if (!mem.TryRead(current, node, kGymReadMode))
            break;
        ++lastObjectCount_;

        const std::uint64_t behaviour = inspectObject(node.object);
        if (Utils::valid_pointer(behaviour))
            return behaviour;

        if (current == activeNode)
        {
            reachedActiveNode = true;
            break;
        }
        current = node.previous;
    }

    if (!reachedActiveNode)
    {
        current = activeNode;
        while (ShouldRead(generation) && Utils::valid_pointer(current) && lastObjectCount_ < kMaxGameObjects && visited.emplace(current).second)
        {
            GymObjectListNode node{};
            if (!mem.TryRead(current, node, kGymReadMode))
                break;
            ++lastObjectCount_;

            const std::uint64_t behaviour = inspectObject(node.object);
            if (Utils::valid_pointer(behaviour))
                return behaviour;

            if (current == lastNode)
                break;
            current = node.next;
        }
    }

    progress = {};
    PublishLastHideoutScan(progress);
    PublishSnapshot(progress, generation);
    return 0;
}

std::string Gym::ReadBehaviourClassName(std::uint64_t behaviour) const
{
    if (!Utils::valid_pointer(behaviour))
        return {};

    std::uint64_t klass = 0;
    std::uint64_t namePointer = 0;
    if (!mem.TryRead(behaviour, klass, kGymReadMode) || !Utils::valid_pointer(klass) ||
        !mem.TryRead(klass + 0x10, namePointer, kGymReadMode) || !Utils::valid_pointer(namePointer))
    {
        return {};
    }

    return mem.readString(namePointer, 64, kGymReadMode);
}

void Gym::PublishLastHideoutScan(GymSnapshot& state) const
{
    state.hideoutScanAttempted = lastHideoutScan_ != std::chrono::steady_clock::time_point{};
    state.gameObjectManager = lastGameObjectManager_;
    state.gameObjectManagerValid = Utils::valid_pointer(lastGameObjectManager_);
    state.objectListValid = lastObjectListValid_;
    state.objectCount = lastObjectCount_;
}

std::uint64_t Gym::ResolveTypeInfoTable() const
{
    const auto pointers = mem.GetTarkovPointerSnapshot();
    const std::uint64_t gameAssembly = pointers.gameAssemblyBase;

    if (!Utils::valid_pointer(gameAssembly))
        return 0;

    std::uint64_t typeInfoTable = 0;
    (void)mem.TryRead(gameAssembly + UnityOffsets::GameWorld, typeInfoTable, kGymReadMode);

    if (!Utils::valid_pointer(typeInfoTable))
        return 0;

    return typeInfoTable;
}

std::uint64_t Gym::ResolveClass(std::uint64_t typeInfoTable, std::int32_t typeIndex) const
{
    if (!Utils::valid_pointer(typeInfoTable) || typeIndex < 0)
        return 0;

    const std::uint64_t entryAddress = typeInfoTable + (static_cast<std::uint64_t>(typeIndex) * sizeof(std::uint64_t));
    std::uint64_t klass = 0;
    (void)mem.TryRead(entryAddress, klass, kGymReadMode);

    if (!Utils::valid_pointer(klass))
        return 0;

    return klass;
}

std::uint64_t Gym::ResolveOverlayStaticFields(std::uint64_t overlayClass, std::uint64_t qteControllerClass,
                                              std::uint64_t& resolvedOffset) const
{
    resolvedOffset = 0;

    if (!Utils::valid_pointer(overlayClass))
        return 0;

    std::uint64_t fallbackStaticFields = 0;
    (void)mem.TryRead(overlayClass + kIl2CppClassStaticFields, fallbackStaticFields, kGymReadMode);

    if (Utils::valid_pointer(fallbackStaticFields))
    {
        std::uint64_t controller = 0;
        (void)mem.TryRead(fallbackStaticFields + sdk::HideoutAreaQTEOverlay::QteController, controller, kGymReadMode);
        if (IsObjectOfClass(controller, qteControllerClass))
        {
            resolvedOffset = kIl2CppClassStaticFields;
            return fallbackStaticFields;
        }
    }

    if (!Utils::valid_pointer(qteControllerClass))
        return 0;

    for (std::uint64_t offset = 0x80; offset <= 0xE0; offset += 0x08)
    {
        if (offset == kIl2CppClassStaticFields)
            continue;

        std::uint64_t staticFields = 0;
        std::uint64_t controller = 0;
        (void)mem.TryRead(overlayClass + offset, staticFields, kGymReadMode);

        if (!Utils::valid_pointer(staticFields))
            continue;

        (void)mem.TryRead(staticFields + sdk::HideoutAreaQTEOverlay::QteController, controller, kGymReadMode);

        if (!IsObjectOfClass(controller, qteControllerClass))
            continue;

        resolvedOffset = offset;
        return staticFields;
    }

    if (Utils::valid_pointer(fallbackStaticFields))
    {
        resolvedOffset = kIl2CppClassStaticFields;
        return fallbackStaticFields;
    }

    return 0;
}

bool Gym::IsObjectOfClass(std::uint64_t object, std::uint64_t expectedClass) const
{
    if (!Utils::valid_pointer(object) ||
        !Utils::valid_pointer(expectedClass))
    {
        return false;
    }

    std::uint64_t runtimeClass = 0;
    (void)mem.TryRead(object, runtimeClass, kGymReadMode);

    return runtimeClass == expectedClass;
}

bool Gym::ReadSpawnedList(std::uint64_t controller, std::uint64_t& list, std::int32_t& count) const
{
    list = 0;
    count = 0;

    if (!Utils::valid_pointer(controller))
        return false;

    if (!mem.TryRead(controller + sdk::QTEController::SpawnedQtes, list, kGymReadMode))
        return false;

    if (!Utils::valid_pointer(list))
        return false;

    if (!mem.TryRead(list + UnityList<std::uint64_t>::CountOffset, count, kGymReadMode))
        return false;

    if (count < 0 || count > kMaxSpawnedQtes)
        return false;

    if (count == 0)
        return true;

    std::uint64_t items = 0;
    if (!mem.TryRead(list + UnityList<std::uint64_t>::ArrOffset, items, kGymReadMode))
        return false;

    if (!Utils::valid_pointer(items))
        return false;

    return true;
}

std::uint64_t Gym::FindShrinkingCircle(std::uint64_t list, std::int32_t count, std::uint64_t shrinkingCircleClass,
                                       std::string& observedClasses, bool& matchedByName) const
{
    observedClasses.clear();
    matchedByName = false;

    if (!Utils::valid_pointer(list) || !Utils::valid_pointer(shrinkingCircleClass) || count <= 0)
        return 0;

    const UnityList<std::uint64_t> spawnedQtes = UnityList<std::uint64_t>::Create(list, kGymReadMode, kMaxSpawnedQtes);
    if (spawnedQtes.count() != count)
        return 0;

    for (const std::uint64_t object : spawnedQtes)
    {
        if (!Utils::valid_pointer(object))
            continue;

        if (IsObjectOfClass(object, shrinkingCircleClass))
            return object;

        const std::string className = ReadBehaviourClassName(object);
        if (!className.empty())
        {
            if (!observedClasses.empty())
                observedClasses += ", ";
            observedClasses += className;
        }
        if (_stricmp(className.c_str(), "ShrinkingCircleQTE") == 0)
        {
            matchedByName = true;
            return object;
        }
    }

    return 0;
}

std::uint64_t Gym::FindShrinkingCircleBehaviour(std::uint64_t startNode, std::uint64_t shrinkingCircleClass,
                                                std::int32_t& objectsScanned, std::uint64_t& nextNode,
                                                std::uint64_t stopNode, std::uint64_t generation) const
{
    objectsScanned = 0;
    nextNode = 0;
    if (!Utils::valid_pointer(startNode) || !ShouldRead(generation))
        return 0;

    std::uint64_t current = startNode;
    std::unordered_set<std::uint64_t> visited;
    visited.reserve(kDirectQteChunkObjects);
    while (ShouldRead(generation) && Utils::valid_pointer(current) && current != stopNode &&
           objectsScanned < kDirectQteChunkObjects && visited.emplace(current).second)
    {
        GymObjectListNode node{};
        if (!mem.TryRead(current, node, kGymReadMode))
        {
            current = 0;
            break;
        }
        ++objectsScanned;

        if (Utils::valid_pointer(node.object))
        {
            GymComponentArray components{};
            if (mem.TryRead(node.object + UnityOffsets::GameObject_ComponentsOffset, components, kGymReadMode) &&
                Utils::valid_pointer(components.entries) && components.size > 0 && components.size <= 0x400)
            {
                std::vector<GymComponentEntry> entries(static_cast<std::size_t>(components.size));
                if (mem.Read(components.entries, entries.data(), entries.size() * sizeof(GymComponentEntry), kGymReadMode, "Gym live QTE components"))
                {
                    for (const GymComponentEntry& entry : entries)
                    {
                        if (!Utils::valid_pointer(entry.component))
                            continue;
                        std::uint64_t behaviour = 0;
                        if (!mem.TryRead(entry.component + kEftComponentObjectClassOffset, behaviour, kGymReadMode) || !Utils::valid_pointer(behaviour))
                            continue;
                        if (IsObjectOfClass(behaviour, shrinkingCircleClass) || _stricmp(ReadBehaviourClassName(behaviour).c_str(), "ShrinkingCircleQTE") == 0)
                        {
                            nextNode = node.previous;
                            return behaviour;
                        }
                    }
                }
            }
        }

        current = node.previous;
    }

    nextNode = current;
    return 0;
}

void Gym::ResetCircleTransformCache()
{
    cachedTransformCircle_ = 0;
    cachedTransformAccess_ = 0;
    cachedTransformData_ = 0;
    cachedTransformVertices_ = 0;
    cachedTransformIndex_ = -1;
    cachedScaleResolver_.clear();
    lastTransformResolve_ = {};
}

bool Gym::ResolveCircleTransform(std::uint64_t circle, std::uint64_t managedTransform,
                                 std::uint64_t dynamicImage, GymSnapshot& state)
{
    if (cachedTransformCircle_ != circle)
        ResetCircleTransformCache();

    if (cachedTransformCircle_ == circle && Utils::valid_pointer(cachedTransformAccess_) &&
        Utils::valid_pointer(cachedTransformData_) && Utils::valid_pointer(cachedTransformVertices_) &&
        cachedTransformIndex_ >= 0)
    {
        state.dynamicCircleTransform = cachedTransformAccess_;
        state.transformData = cachedTransformData_;
        state.transformVertices = cachedTransformVertices_;
        state.transformIndex = cachedTransformIndex_;
        state.transformAccessValid = true;
        state.scaleResolver = cachedScaleResolver_;
        return true;
    }

    const auto now = std::chrono::steady_clock::now();
    if (lastTransformResolve_ != std::chrono::steady_clock::time_point{} &&
        now - lastTransformResolve_ < kTransformResolveInterval)
    {
        return false;
    }
    lastTransformResolve_ = now;

    std::uint64_t managedNative = 0;
    if (Utils::valid_pointer(managedTransform))
    {
        (void)mem.TryRead(managedTransform + UnityOffsets::ManagedObject_NativePointerOffset, managedNative, kGymReadMode);
        if (TryCacheTransformAccess(managedNative, circle, "inner border m_CachedPtr", state))
            return true;
        if (TryCacheTransformAccess(managedTransform, circle, "inner border native", state))
            return true;
    }

    if (Utils::valid_pointer(dynamicImage))
    {
        std::uint64_t resolvedTransform = 0;
        if (UnityTransform::TryResolveFromComponent(dynamicImage, resolvedTransform, false) &&
            TryCacheTransformAccess(resolvedTransform, circle, "image GameObject Transform", state))
        {
            return true;
        }
    }

    return false;
}

bool Gym::TryCacheTransformAccess(std::uint64_t candidate, std::uint64_t circle,
                                  const char* resolver, GymSnapshot& state)
{
    if (!Utils::valid_pointer(candidate))
        return false;

    std::uint64_t transformData = 0;
    std::int32_t transformIndex = -1;
    ScatterReadBatch accessRead(mem, kGymReadMode, "Gym transform access");
    if (!accessRead.Add(candidate + UnityOffsets::TransformAccess_HierarchyOffset, transformData) ||
        !accessRead.Add(candidate + UnityOffsets::TransformAccess_IndexOffset, transformIndex) ||
        !accessRead.Execute())
    {
        return false;
    }

    if (transformIndex < 0 || transformIndex >= kMaxTransformIndex)
        return false;

    std::uint64_t vertices = 0;
    const auto resolveHierarchy = [&](std::uint64_t data)
    {
        if (!Utils::valid_pointer(data))
            return false;

        std::uint64_t indices = 0;
        ScatterReadBatch hierarchyRead(mem, kGymReadMode, "Gym transform hierarchy");
        return hierarchyRead.Add(data + UnityOffsets::Hierarchy_VerticesOffset, vertices) &&
               hierarchyRead.Add(data + UnityOffsets::Hierarchy_IndicesOffset, indices) &&
               hierarchyRead.Execute() && Utils::valid_pointer(vertices) && Utils::valid_pointer(indices);
    };

    std::string resolvedBy = resolver ? resolver : "native transform";
    if (!resolveHierarchy(transformData))
    {
        // Some current Unity builds expose the transform-data pointer again at
        // +0x90. dma-radar uses this location for its lightweight validation.
        std::uint64_t alternateData = 0;
        (void)mem.TryRead(candidate + 0x90, alternateData, kGymReadMode);
        vertices = 0;
        if (!resolveHierarchy(alternateData))
            return false;

        transformData = alternateData;
        resolvedBy += " +0x90 data";
    }

    cachedTransformCircle_ = circle;
    cachedTransformAccess_ = candidate;
    cachedTransformData_ = transformData;
    cachedTransformVertices_ = vertices;
    cachedTransformIndex_ = transformIndex;
    cachedScaleResolver_ = std::move(resolvedBy);

    state.dynamicCircleTransform = candidate;
    state.transformData = transformData;
    state.transformVertices = vertices;
    state.transformIndex = transformIndex;
    state.transformAccessValid = true;
    state.scaleResolver = cachedScaleResolver_;
    return true;
}

bool Gym::ReadCachedCircleScale(GymVec3& scale) const
{
    scale = {};
    if (!Utils::valid_pointer(cachedTransformVertices_) || cachedTransformIndex_ < 0 ||
        cachedTransformIndex_ >= kMaxTransformIndex)
    {
        return false;
    }

    UnityTransform::TrsX transform{};
    const std::uint64_t address = cachedTransformVertices_ +
        static_cast<std::uint64_t>(cachedTransformIndex_) * sizeof(UnityTransform::TrsX);
    if (!mem.TryRead(address, transform, kGymReadMode))
        return false;

    scale = { transform.s.x, transform.s.y, transform.s.z };
    return true;
}

void Gym::DumpState() const
{
    const GymSnapshot state = GetSnapshot();

    std::cout
        << "\n========== Gym QTE ==========\n"
        << std::hex << std::showbase
        << "GameObjectManager:      " << state.gameObjectManager << '\n'
        << "Hideout marker:         " << state.hideoutArea << '\n'
        << "TypeInfoTable:         " << state.typeInfoTable << '\n'
        << "Overlay class:         " << state.overlayClass << '\n'
        << "QTEController class:   " << state.qteControllerClass << '\n'
        << "Shrinking class:       " << state.shrinkingCircleClass << '\n'
        << "static_fields:         " << state.staticFields << '\n'
        << "static_fields offset:  " << state.staticFieldsOffset << '\n'
        << "QTEController:         " << state.qteController << '\n'
        << "_spawnedQtes:          " << state.spawnedQtes << '\n'
        << "ShrinkingCircleQTE:    " << state.shrinkingCircle << '\n'
        << "Dynamic circle image:  " << state.dynamicCircleImage << '\n'
        << "Dynamic transform:     " << state.dynamicCircleTransform << '\n'
        << "Dynamic inner border:  " << state.dynamicCircleInnerBorder << '\n'
        << "Transform data:        " << state.transformData << '\n'
        << "Transform vertices:    " << state.transformVertices << '\n'
        << std::dec << std::noshowbase
        << "Matched class:          " << state.hideoutClassName << '\n'
        << "Circle resolver:        " << state.circleResolver << '\n'
        << "Scale resolver:         " << state.scaleResolver << '\n'
        << "Transform index:        " << state.transformIndex << '\n'
        << "Transform valid:        " << (state.transformAccessValid ? "true" : "false") << '\n'
        << "Spawned runtime types:  " << state.spawnedClassNames << '\n'
        << "Objects scanned:        " << state.objectCount << '\n'
        << "Direct objects checked: " << state.directObjectScanCount << '\n'
        << "Spawned count:         " << state.spawnedCount << '\n'
        << "Speed:                 " << state.speed << '\n'
        << "SuccessRange:          "
        << state.successRange.x << ", "
        << state.successRange.y << '\n'
        << "Active success window: " << state.successWindowMinimum << ", " << state.successWindowMaximum << '\n'
        << "Min scale:             " << state.minScale << '\n'
        << "Current circle scale:  " << state.circleScale.x << ", " << state.circleScale.y << ", " << state.circleScale.z << '\n'
        << "Predicted success:     " << (state.predictedSuccess ? "true" : "false") << '\n'
        << "Success start scale:   " << state.successStartScale << '\n'
        << "Success end scale:     " << state.successEndScale << '\n'
        << "Target input:          " << state.targetInput << '\n'
        << "Auto click status:     " << state.autoClickStatus << '\n'
        << "Last click event:      " << state.lastAutoClickEvent << '\n'
        << "IsSuccess:             "
        << (state.isSuccess ? "true" : "false") << '\n'
        << "Active:                "
        << (state.shrinkingCircleValid ? "true" : "false") << '\n'
        << "=============================\n";
}
