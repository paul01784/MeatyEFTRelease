#include "app/includes.h"
#include "app/globals.h"


//define globals
std::string globals::appVersion = "1.0.19";
float globals::appTextScale = 1.f;
float globals::appWindowAlpha = 0.7f;
float globals::appRadarMaxFPS = 60.f;
std::string globals::dogTagAPIKey = "";

std::string globals::radarSubText = "";

double globals::taskMainPlayers = 3000;
double globals::taskPlayers = 33;
double globals::taskPlayersBones = 33;
double globals::taskPlayersLocal = 22;
double globals::taskGrenades = 200;
double globals::taskPlayersEquipment = 2000;
double globals::taskExfil = 5000;
double globals::taskLoot = 8000;
double globals::taskQuest = 7000;
double globals::taskWishManager = 1000;
double globals::taskTripWire = 20;
double globals::taskKeyManager = 25;
double globals::taskCamera = 33;
double globals::taskRaidMonitor = 800;
double globals::taskWrites = 50;
double globals::taskAim = 5;

// App menu settings/status
bool appMenu::appSettings = false;
bool appMenu::appLootFilters = false;
bool appMenu::appFuser = false;
bool appMenu::appMakcu = false;

bool appMenu::appQuests = false;

bool appMenu::widgetPlayers = true;
bool appMenu::widgetTopLoot = false;
bool appMenu::widgetExfil = false;
bool appMenu::widgetExfil_Scav = false;
bool appMenu::widgetLoot = false;
bool appMenu::widgetDebug = false;

bool appMenu::minView = false;

// mapGlobals struct defaults/current values
bool mapGlobals::followLocal = true; //follow local as default, maybe reset this when need to avoid offmap display on new maps
glm::vec3 mapGlobals::focusPoint = glm::vec3(0, 0, 0);

// gameGlobals struct
bool gameGlobals::gameRunning = false;
bool gameGlobals::inHideout = false;


// appGlobals struct
std::atomic_bool appGlobals::runRadar = false;
std::atomic_bool appGlobals::runThreads = false;

// memoryGlobals struct
std::atomic_bool memoryGlobals::dmaConnected{ false };
std::atomic_bool memoryGlobals::processFound{ false };
bool memoryGlobals::dmaAutoConnect = false;
bool memoryGlobals::dmaCloseAll = true;
bool memoryGlobals::dmaShowStats = true;


bool radarGlobals::drawPlayers = false;
bool radarGlobals::drawLoot = false;
bool radarGlobals::drawQuestHelper = false;
bool radarGlobals::drawGrenades = false;
bool radarGlobals::drawExfils = false;
int radarGlobals::localAimLine = 100;
int radarGlobals::friendAimLine = 100;
int radarGlobals::enemyAimLine = 100;
bool radarGlobals::getPlayerEquip = false;
bool radarGlobals::getPlayerStats = false;

bool espGlobals::espEnabled = false;
bool espGlobals::drawPlayers = false;
int espGlobals::drawPlayerDist = 200;
int espGlobals::drawCorpseDist = 100;
bool espGlobals::drawGrenades = false;
int espGlobals::drawGrenadesDist = 100;
bool espGlobals::drawLoot = false;
bool espGlobals::drawCorpse = false;
int espGlobals::drawLootDist = 40;
bool espGlobals::drawQuestHelper = false;
bool espGlobals::drawBoxPlayers = false;
bool espGlobals::drawHealthPlayers = false;

glm::vec2 espGlobals::gameRes = glm::vec2(1920.f, 1080.f);
int espGlobals::gameResInt = 0;
bool espGlobals::drawSkeletons = false;
bool espGlobals::skeletonsOnlyClosest = false;
bool espGlobals::drawCrosshair = false;
bool espGlobals::drawHeadDot = false;
float espGlobals::headDotSize = 1.5f;
bool espGlobals::runEsp = false;

bool aimGlobals::aimEnabled = false;
float aimGlobals::aimFOV = 50.f;
int aimGlobals::aimDistance = 100;
boneListIndexes aimGlobals::aiBone = boneListIndexes::Head;
boneListIndexes aimGlobals::pmcBone = boneListIndexes::Head;
TargetMode aimGlobals::targetMode = TargetMode::FOV;
bool aimGlobals::targetLock = false;
float aimGlobals::aimSmooth = 4.f;


glm::vec4 coloursGlobals::playerPMC = { 1,1,1,1 };
glm::vec4 coloursGlobals::playerScav = { 1,1,1,1 };
glm::vec4 coloursGlobals::playerAI = glm::vec4(1, 1, 1, 1);
glm::vec4 coloursGlobals::playerBoss = glm::vec4(1, 1, 1, 1);
glm::vec4 coloursGlobals::aiBTR = glm::vec4(1, 1, 1, 1);
glm::vec4 coloursGlobals::playerWatched = glm::vec4(1, 1, 1, 1);
glm::vec4 coloursGlobals::playerFriendly = glm::vec4(1, 1, 1, 1);
glm::vec4 coloursGlobals::playerLocal = glm::vec4(1, 1, 1, 1);
glm::vec4 coloursGlobals::playerSkeleton = glm::vec4(1, 1, 1, 1);
glm::vec4 coloursGlobals::playerCorpse = glm::vec4(1, 1, 1, 1);
glm::vec4 coloursGlobals::playerGroupLine = glm::vec4(1, 1, 1, 1);
glm::vec4 coloursGlobals::grenades = glm::vec4(1, 1, 1, 1);
glm::vec4 coloursGlobals::exfils = glm::vec4(1, 1, 1, 1);
glm::vec4 coloursGlobals::questMarker = glm::vec4(1, 1, 1, 1);
glm::vec4 coloursGlobals::crosshair = glm::vec4(1, 1, 1, 1);
glm::vec4 coloursGlobals::fovCircle = glm::vec4(1, 1, 1, 1);
glm::vec4 coloursGlobals::questColour = glm::vec4(1, 1, 1, 1);
glm::vec4 coloursGlobals::wishListColour = glm::vec4(1, 1, 1, 1);
glm::vec4 coloursGlobals::valueLootColour = glm::vec4(1, 1, 1, 1);

WindowsKey keyGlobals::aimKey = WindowsKey::LeftControl;
WindowsKey keyGlobals::toggleFollow = WindowsKey::Enter;
WindowsKey keyGlobals::espToggle = WindowsKey::F12;

bool lootGlobals::enableQuestLoot = false;
bool lootGlobals::enableWishListLoot = false;
bool lootGlobals::enableValueLoot = false;
int lootGlobals::valueLootFrom = 0;