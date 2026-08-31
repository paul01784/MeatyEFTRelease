#pragma once
#include <chrono>
#include <vector>

#include "render.h"
#include "../external/glm/glm.hpp"


struct globals {
    static std::string appVersion;
    static std::string latestAppVersion;
    static bool showVersionMismatchWarning;
    static float appTextScale;
    static float appWindowAlpha;
    static float appRadarMaxFPS;
    static std::string dogTagAPIKey;

    static std::string radarSubText;

    static double taskPlayers;
    static double taskPlayerPositions;
    static double taskPlayersBones;
    static double taskStaticVisibility;
    static double taskGrenades;
    static double taskPlayersEquipment;
    static double taskPlayerMetadata;
    static double taskExfil;
    static double taskLoot;
    static double taskQuest;
    static double taskWishManager;
    static double taskTripWire;
    static double taskKeyManager;
    static double taskCamera;
    static double taskFireport;
    static double taskMemoryManager;
    static double taskRaidMonitor;
    static double taskAim;

};

struct appMenu {

    static bool appSettings;
    static bool appFuser;
    static bool appMakcu;
    static bool appLootFilters;
    static bool appQuests;
    static bool appWatchList;

    static bool widgetPlayers;
    static bool widgetTopLoot;
    static bool widgetExfil;
    static bool widgetExfil_Scav;
    static bool widgetLoot;
    static bool widgetDebug;
};



struct mapGlobals {
    static bool followLocal;
    static glm::vec3 focusPoint;
    static glm::vec3 lootFocusRippleLocation;
    static std::chrono::steady_clock::time_point lootFocusRippleUntil;

    static void startLootFocusRipple(const glm::vec3& location);
};


struct gameGlobals {
    static bool gameRunning;
    static bool inHideout;
};

struct appGlobals {
    static std::atomic_bool runRadar;
    static std::atomic_bool runThreads;
};

struct memoryGlobals {
    static std::atomic_bool dmaConnected;
    static std::atomic_bool processFound;
    static bool dmaAutoConnect;
    static bool dmaCloseAll;
    static bool dmaShowStats;
};

struct radarGlobals {
    static bool minimalView;
    static bool drawPlayers;
    static bool drawLoot;
    static bool drawQuestHelper;
    static bool drawGrenades;
    static bool drawTripwires;
    static bool drawTripwireLine;
    static bool drawExfils;
    static bool drawSecretExfils;
    static bool drawTransitExfils;
    static int localAimLine;
    static int friendAimLine;
    static int enemyAimLine;
    static bool drawAimLineTargets;
    static float aimLineTargetAngle;
    static int aimLineTargetMaxDistance;
    static bool getPlayerEquip;
    static bool getPlayerStats;
    // 0 = persistent PvP, 1 = PvP Seasonal.
    static int tarkovDevDataMode;
    static float textScale;
    static int fontIndex;
    static bool fontBold;

};

struct espGlobals {
    static bool espEnabled;
    static bool drawPlayers;
    static int drawPlayerDist;
    // 0 = off, 1 = all valid sources, 2 = PMCs and player scavs only.
    static int aimOverlayAlert;
    static int drawCorpseDist;
    static bool drawGrenades;
    static int drawGrenadesDist;
    static bool drawTripwires;
    static int drawTripwiresDist;
    static bool drawLoot;
    static bool drawCorpse;
    static bool drawQuestHelper;
    static int drawLootDist;
    static bool drawBoxPlayers;
    static bool drawHealthPlayers;
    static glm::vec2 gameRes;
    static int gameResInt;
    static bool drawSkeletons;
    static bool drawCrosshair;
    // 0 = dot/circle, 1 = cross.
    static int crosshairType;
    static int crosshairSize;
    static bool drawHeadDot;
    static float headDotSize;
    static bool runEsp;
    static int drawExfilDist;
    static bool drawExfil;
    static bool drawSecretExfils;
    static bool drawTransitExfils;
};

struct aimGlobals {
    static bool aimEnabled;
    static bool predictionEnabled;
    static float aimFOV;
    static int aimDistance;
    static boneListIndexes aiBone;
    static boneListIndexes pmcBone;
    static TargetMode targetMode;
    static bool targetLock;
    static float aimSmooth;
    static float aimSpeedPixelsPerSecond;
    static float aimDeadzonePixels;
    static float aimOffsetX;
    static float aimOffsetY;
    static AimReference aimReference;
    static bool aimClosestBoneToFireport;
    static bool showAimFovRing;
    static bool drawFireportLine;
};

struct coloursGlobals {
    static glm::vec4 playerPMC;
    static glm::vec4 playerScav;
    static glm::vec4 playerAI;
    static glm::vec4 playerBoss;
    static glm::vec4 playerBlackDiv;
    static glm::vec4 aiBTR;
    static glm::vec4 playerWatched;
    static glm::vec4 playerFriendly;
    static glm::vec4 playerLocal;
    static glm::vec4 playerSkeleton;
    static glm::vec4 playerCorpse;
    static glm::vec4 playerGroupLine;
    static glm::vec4 grenades;
    static glm::vec4 tripwires;
    static glm::vec4 exfils;
    static glm::vec4 questMarker;
    static glm::vec4 crosshair;
    static glm::vec4 fovCircle;
    static glm::vec4 questColour;
    static glm::vec4 wishListColour;
    static glm::vec4 valueLootColour;
    static glm::vec4 containerColour;
};

struct keyGlobals {
    static WindowsKey aimKey;
    static WindowsKey toggleFollow;
    static WindowsKey battleMode;
    static WindowsKey toggleRadarMinView;
};

struct lootGlobals {
    static bool enableQuestLoot;
    static bool enableWishListLoot;
    static bool enableValueLoot;
    static bool showLootValue;
    static int lootValuePriceSource;
    static std::vector<std::string> selectedLootCategories;
    static glm::vec4 categoryLootColour;
    static bool hideSearched;
    static int containerDistance;
    static int valueLootFrom;
    static int valueLootFromEquip;
    static bool drawDrawer;
    static bool drawDuffle;
    static bool drawSafe;
    static bool drawWeaponBox;
    static bool drawTechCrate;
    static bool drawRationCrate;
    static bool drawMedicalCrate;
    static bool drawJacket;
    static bool drawMedPackage;
    static bool drawMedBox;
    static bool drawToolbox;
    static bool drawGrenadeBox;
    static bool drawBuriedStash;
    static bool drawGroundCache;
    static bool drawWoodenCrate;
    static bool drawSuitcase;
    static bool drawAmmoBox;
    static bool drawDeadBody;
    static bool drawPCBlock;
    static bool drawRegister;
    static bool drawAirDrops;
};

struct writeGlobals {
    static bool infStam;
};
