#pragma once
#include "render.h"
#include "../external/glm/glm.hpp"


struct globals {
    static std::string appVersion;
    static float appTextScale;
    static float appWindowAlpha;
    static float appRadarMaxFPS;
    static std::string dogTagAPIKey;

    static std::string radarSubText;

    static double taskPlayers;
    static double taskPlayersBones;
    static double taskGrenades;
    static double taskPlayersEquipment;
    static double taskExfil;
    static double taskLoot;
    static double taskQuest;
    static double taskWishManager;
    static double taskTripWire;
    static double taskKeyManager;
    static double taskCamera;
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

    static bool minView;
};



struct mapGlobals {
    static bool followLocal;
    static glm::vec3 focusPoint;
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
    static bool drawPlayers;
    static bool drawLoot;
    static bool drawQuestHelper;
    static bool drawGrenades;
    static bool drawExfils;
    static int localAimLine;
    static int friendAimLine;
    static int enemyAimLine;
    static bool getPlayerEquip;
    static bool getPlayerStats;

};

struct espGlobals {
    static bool espEnabled;
    static bool drawPlayers;
    static int drawPlayerDist;
    static int drawCorpseDist;
    static bool drawGrenades;
    static int drawGrenadesDist;
    static bool drawLoot;
    static bool drawCorpse;
    static bool drawQuestHelper;
    static int drawLootDist;
    static bool drawBoxPlayers;
    static bool drawHealthPlayers;
    static glm::vec2 gameRes;
    static int gameResInt;
    static bool drawSkeletons;
    static bool skeletonsOnlyClosest;
    static bool drawCrosshair;
    static bool drawHeadDot;
    static float headDotSize;
    static bool runEsp;
    static bool drawFireportLine;
    static int drawExfilDist;
    static bool drawExfil;
};

struct aimGlobals {
    static bool aimEnabled;
    static float aimFOV;
    static int aimDistance;
    static boneListIndexes aiBone;
    static boneListIndexes pmcBone;
    static TargetMode targetMode;
    static bool targetLock;
    static float aimSmooth;
    static AimReference aimReference;
    static bool showAimFovRing;
    static float fireportLineLengthM;
};

struct coloursGlobals {
    static glm::vec4 playerPMC;
    static glm::vec4 playerScav;
    static glm::vec4 playerAI;
    static glm::vec4 playerBoss;
    static glm::vec4 aiBTR;
    static glm::vec4 playerWatched;
    static glm::vec4 playerFriendly;
    static glm::vec4 playerLocal;
    static glm::vec4 playerSkeleton;
    static glm::vec4 playerCorpse;
    static glm::vec4 playerGroupLine;
    static glm::vec4 grenades;
    static glm::vec4 exfils;
    static glm::vec4 questMarker;
    static glm::vec4 crosshair;
    static glm::vec4 fovCircle;
    static glm::vec4 questColour;
    static glm::vec4 wishListColour;
    static glm::vec4 valueLootColour;
};

struct keyGlobals {
    static WindowsKey aimKey;
    static WindowsKey toggleFollow;
    static WindowsKey battleMode;


};

struct lootGlobals {
    static bool enableQuestLoot;
    static bool enableWishListLoot;
    static bool enableValueLoot;
    static int valueLootFrom;
};

struct writeGlobals {
    static bool infStam;
};
