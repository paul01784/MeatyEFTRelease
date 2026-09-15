#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <stop_token>
#include <string>
#include <thread>

struct GymVec2
{
    float x{};
    float y{};
};

struct GymVec3
{
    float x{};
    float y{};
    float z{};
};

struct GymSnapshot
{
    bool hideoutScanAttempted{};
    bool gameObjectManagerValid{};
    bool objectListValid{};
    bool hideoutAreaValid{};
    bool typeInfoValid{};
    bool overlayClassValid{};
    bool qteControllerClassValid{};
    bool shrinkingCircleClassValid{};
    bool staticFieldsValid{};
    bool controllerValid{};
    bool spawnedListValid{};
    bool shrinkingCircleValid{};
    bool circleResolvedByObjectScan{};
    bool directObjectScanAttempted{};
    bool autoClickEnabled{};
    bool makcuConnected{};
    bool lastAutoClickSucceeded{};
    bool circleScaleValid{};
    bool transformAccessValid{};
    bool predictedSuccess{};
    bool calculatedSuccessWindow{};

    // UI/readiness state. These do not participate in the resolver decisions.
    bool resolverReady{};
    bool readyToStart{};
    bool staticFieldsResolved{};
    bool controllerSpawned{};

    std::uint64_t gameObjectManager{};
    std::uint64_t hideoutArea{};
    std::uint64_t typeInfoTable{};
    std::uint64_t overlayClass{};
    std::uint64_t qteControllerClass{};
    std::uint64_t shrinkingCircleClass{};
    std::uint64_t staticFields{};
    std::uint64_t qteController{};
    std::uint64_t spawnedQtes{};
    std::uint64_t shrinkingCircle{};
    std::uint64_t dynamicCircleImage{};
    std::uint64_t dynamicCircleInnerBorder{};
    std::uint64_t dynamicCircleTransform{};
    std::uint64_t transformData{};
    std::uint64_t transformVertices{};

    std::uint64_t staticFieldsOffset{};

    std::int32_t objectCount{};
    std::int32_t directObjectScanCount{};
    std::int32_t spawnedCount{};
    std::int32_t targetInput{};
    std::int32_t transformIndex{ -1 };

    float speed{};
    float minScale{};
    float successWindowMinimum{};
    float successWindowMaximum{};
    GymVec2 successRange{};
    GymVec3 circleScale{};

    double successStartScale{};
    double successEndScale{};

    bool isSuccess{};

    std::uint64_t autoClickCount{};

    std::string hideoutClassName;
    std::string circleResolver;
    std::string scaleResolver;
    std::string spawnedClassNames;
    std::string autoClickStatus;
    std::string lastAutoClickEvent;
};

class Gym
{
public:
    Gym() = default;
    ~Gym();

    Gym(const Gym&) = delete;
    Gym& operator=(const Gym&) = delete;

    void Configure(bool enabled, bool autoClick, bool inRaid, bool dmaReady);
    void Stop();

    // Returns true only when a live ShrinkingCircleQTE is found.
    bool Update();

    void Reset();
    void DumpState() const;

    [[nodiscard]] bool IsActive() const;
    [[nodiscard]] GymSnapshot GetSnapshot() const;

private:
    static constexpr std::int32_t kMaxSpawnedQtes = 32;
    static constexpr std::int32_t kMaxGameObjects = 100000;
    static constexpr std::int32_t kMaxDirectQteObjects = 512;

    void WorkerLoop(std::stop_token stopToken);
    void ResetWorkerState();
    void PublishSnapshot(GymSnapshot snapshot, std::uint64_t generation);
    [[nodiscard]] bool ShouldRead(std::uint64_t generation) const;

    [[nodiscard]] bool ResolveHideoutArea(GymSnapshot& state, std::uint64_t generation);
    [[nodiscard]] std::uint64_t FindHideoutBehaviour(std::uint64_t gom, std::string& matchedClass, std::uint64_t generation);
    [[nodiscard]] std::string ReadBehaviourClassName(std::uint64_t behaviour) const;
    void PublishLastHideoutScan(GymSnapshot& state) const;

    [[nodiscard]] std::uint64_t ResolveTypeInfoTable() const;
    [[nodiscard]] std::uint64_t ResolveClass(std::uint64_t typeInfoTable, std::int32_t typeIndex) const;

    [[nodiscard]] std::uint64_t ResolveOverlayStaticFields(std::uint64_t overlayClass, std::uint64_t qteControllerClass,
                                                           std::uint64_t& resolvedOffset) const;

    [[nodiscard]] bool IsObjectOfClass(std::uint64_t object, std::uint64_t expectedClass) const;

    [[nodiscard]] bool ReadSpawnedList(std::uint64_t controller, std::uint64_t& list, std::int32_t& count) const;

    [[nodiscard]] std::uint64_t FindShrinkingCircle(std::uint64_t list, std::int32_t count, std::uint64_t shrinkingCircleClass,
                                                    std::string& observedClasses, bool& matchedByName) const;
    [[nodiscard]] std::uint64_t FindShrinkingCircleBehaviour(std::uint64_t startNode, std::uint64_t shrinkingCircleClass,
                                                             std::int32_t& objectsScanned, std::uint64_t& nextNode,
                                                             std::uint64_t stopNode, std::uint64_t generation) const;
    void ResetCircleTransformCache();
    [[nodiscard]] bool ResolveCircleTransform(std::uint64_t circle, std::uint64_t managedTransform,
                                              std::uint64_t dynamicImage, GymSnapshot& state);
    [[nodiscard]] bool TryCacheTransformAccess(std::uint64_t candidate, std::uint64_t circle,
                                               const char* resolver, GymSnapshot& state);
    [[nodiscard]] bool ReadCachedCircleScale(GymVec3& scale) const;

    mutable std::mutex snapshotMutex_;
    GymSnapshot snapshot_{};

    std::mutex workerMutex_;
    std::jthread worker_;
    std::atomic_bool enabled_{ false };
    std::atomic_bool autoClickEnabled_{ false };
    std::atomic_bool inRaid_{ false };
    std::atomic_bool dmaReady_{ false };
    std::atomic_bool runtimeActive_{ false };
    std::atomic_uint64_t generation_{ 0 };

    std::chrono::steady_clock::time_point lastHideoutScan_{};
    bool lastObjectListValid_{};
    std::int32_t lastObjectCount_{};
    std::uint64_t lastGameObjectManager_{};
    std::uint64_t cachedHideoutArea_{};
    std::uint64_t cachedHideoutRuntimeClass_{};
    std::string cachedHideoutClassName_;

    std::uint64_t lastObservedCircle_{};
    std::uint64_t cachedDirectShrinkingCircle_{};
    std::uint64_t cachedTransformCircle_{};
    std::uint64_t cachedTransformAccess_{};
    std::uint64_t cachedTransformData_{};
    std::uint64_t cachedTransformVertices_{};
    std::int32_t cachedTransformIndex_{ -1 };
    std::string cachedScaleResolver_;
    std::chrono::steady_clock::time_point lastTransformResolve_{};
    std::uint64_t lastDirectTailNode_{};
    std::uint64_t directQteScanCursor_{};
    std::uint64_t directQteStopNode_{};
    std::uint64_t autoClickCount_{};
    std::chrono::steady_clock::time_point lastDirectQteScan_{};
    std::int32_t lastDirectQteObjectCount_{};
    std::uint64_t lastPublishedSpawnedQtes_{};
    bool lastPredictedSuccess_{};
    bool lastAutoClickSucceeded_{};
    std::string lastAutoClickEvent_;
};

extern Gym GYM;
