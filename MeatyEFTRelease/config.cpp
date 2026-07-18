#include "app/includes.h"

#include <glm/glm.hpp>
#include "external/nlohmann/json.hpp"

#include "app/render.h"
#include "app/globals.h"
#include "app/config.h"
#include "game/headers/loot.h"
#include "game/headers/tarkovdevquery.h"
#include "game/headers/utils.h"
#include "app/DxRenderWindow.h"

#include "app/makcu.h"
void ConnectMakcuOnStartup();


namespace fs = std::filesystem;


namespace glm {
    void to_json(nlohmann::json& j, const glm::vec4& v) {
        j = nlohmann::json{ {"r", v.x}, {"g", v.y}, {"b", v.z}, {"a", v.w} };
    }

    void from_json(const nlohmann::json& j, glm::vec4& v) {
        v.x = j.value("r", 1.0f);
        v.y = j.value("g", 1.0f);
        v.z = j.value("b", 1.0f);
        v.w = j.value("a", 1.0f);
    }

    void to_json(nlohmann::json& j, const glm::vec2& v) {
        j = nlohmann::json{ {"x", v.x}, {"y", v.y} };
    }

    void from_json(const nlohmann::json& j, glm::vec2& v) {
        v.x = j.value("x", 1.0f);
        v.y = j.value("y", 1.0f);
    }
}

glm::vec4 get_vec4_or_default(const nlohmann::json& j, const std::string& key, const glm::vec4& default_value) {
    if (j.contains(key) && j[key].is_object()) {
        return j.at(key).get<glm::vec4>();
    }
    return default_value;
}

glm::vec2 get_vec2_or_default(const nlohmann::json& j, const std::string& key, const glm::vec2& default_value) {
    if (j.contains(key) && j[key].is_object()) {
        return j.at(key).get<glm::vec2>();
    }
    return default_value;
}

static nlohmann::json Vec4ToJson(const glm::vec4& v)
{
    return nlohmann::json{
        { "r", v.r },
        { "g", v.g },
        { "b", v.b },
        { "a", v.a }
    };
}

static glm::vec4 JsonToVec4(const nlohmann::json& j, const glm::vec4& fallback)
{
    glm::vec4 v = fallback;

    if (!j.is_object())
        return v;

    v.r = j.value("r", v.r);
    v.g = j.value("g", v.g);
    v.b = j.value("b", v.b);
    v.a = j.value("a", v.a);

    return v;
}

static std::string ConfigWideToUtf8(const std::wstring& text)
{
    if (text.empty())
        return "";

    const int needed = WideCharToMultiByte(
        CP_UTF8,
        0,
        text.c_str(),
        -1,
        nullptr,
        0,
        nullptr,
        nullptr
    );

    if (needed <= 1)
        return "";

    std::string result(static_cast<size_t>(needed - 1), '\0');

    WideCharToMultiByte(
        CP_UTF8,
        0,
        text.c_str(),
        -1,
        result.data(),
        needed,
        nullptr,
        nullptr
    );

    return result;
}

static std::wstring ConfigUtf8ToWide(const std::string& text)
{
    if (text.empty())
        return L"";

    const int needed = MultiByteToWideChar(
        CP_UTF8,
        0,
        text.c_str(),
        -1,
        nullptr,
        0
    );

    if (needed <= 1)
        return L"";

    std::wstring result(static_cast<size_t>(needed - 1), L'\0');

    MultiByteToWideChar(
        CP_UTF8,
        0,
        text.c_str(),
        -1,
        result.data(),
        needed
    );

    return result;
}

static nlohmann::json ConfigVec4ToJson(const glm::vec4& v)
{
    return nlohmann::json{
        { "r", v.r },
        { "g", v.g },
        { "b", v.b },
        { "a", v.a }
    };
}

static glm::vec4 ConfigJsonToVec4(const nlohmann::json& j, const glm::vec4& fallback)
{
    glm::vec4 v = fallback;

    if (!j.is_object())
        return v;

    v.r = j.value("r", v.r);
    v.g = j.value("g", v.g);
    v.b = j.value("b", v.b);
    v.a = j.value("a", v.a);

    return v;
}


// Custom serialization for WindowsKey
void to_json(nlohmann::json& j, const WindowsKey& k) {
    j = static_cast<int>(k);
}

void from_json(const nlohmann::json& j, WindowsKey& k) {
    k = static_cast<WindowsKey>(j.get<int>());
}

// Custom serialization for BoneList
void to_json(nlohmann::json& j, const BoneList& k) {
    j = static_cast<int>(k);
}

void from_json(const nlohmann::json& j, BoneList& k) {
    k = static_cast<BoneList>(j.get<int>());
}

// Custom serialization for TargetMode
void to_json(nlohmann::json& j, const TargetMode& k) {
    j = static_cast<int>(k);
}

void from_json(const nlohmann::json& j, TargetMode& k) {
    k = static_cast<TargetMode>(j.get<int>());
}

// Custom serialization for Globals
void to_json(nlohmann::json& j, const globals& r) {
    j = nlohmann::json{
        {"appWindowAlpha", r.appWindowAlpha},
        {"appRadarMaxFPS", r.appRadarMaxFPS},
        {"dogTagAPIKey", r.dogTagAPIKey}
    };
}

void from_json(const nlohmann::json& j, globals& r) {
    r.appWindowAlpha = j.value("appWindowAlpha", r.appWindowAlpha);
    r.appRadarMaxFPS = j.value("appRadarMaxFPS", r.appRadarMaxFPS);
    r.dogTagAPIKey = j.value("dogTagAPIKey", r.dogTagAPIKey);
}

// Custom serialization for coloursGlobals
void to_json(nlohmann::json& j, const coloursGlobals&) {
    j = nlohmann::json{
        {"playerPMC", coloursGlobals::playerPMC},
        {"playerAI", coloursGlobals::playerAI},
        {"playerScav", coloursGlobals::playerScav},
        {"playerBoss", coloursGlobals::playerBoss},
        {"aiBTR", coloursGlobals::aiBTR},
        {"playerWatched", coloursGlobals::playerWatched},
        {"playerFriendly", coloursGlobals::playerFriendly},
        {"playerLocal", coloursGlobals::playerLocal},
        {"playerCorpse", coloursGlobals::playerCorpse},
        {"playerGroupLine", coloursGlobals::playerGroupLine},
        {"grenades", coloursGlobals::grenades},
        {"exfils", coloursGlobals::exfils},
        {"questMarker", coloursGlobals::questMarker},
        {"crosshair", coloursGlobals::crosshair},
        {"fovCircle", coloursGlobals::fovCircle},
        {"questColour", coloursGlobals::questColour },
        {"wishListColour", coloursGlobals::wishListColour},
        {"valueLootColour", coloursGlobals::valueLootColour }
    };
}

void from_json(const nlohmann::json& j, coloursGlobals& r) {
    r.playerPMC = get_vec4_or_default(j, "playerPMC", r.playerPMC);
    r.playerAI = get_vec4_or_default(j, "playerAI", r.playerAI);
    r.playerScav = get_vec4_or_default(j, "playerScav", r.playerScav);
    r.playerBoss = get_vec4_or_default(j, "playerBoss", r.playerBoss);
    r.aiBTR = get_vec4_or_default(j, "aiBTR", r.aiBTR);
    r.playerWatched = get_vec4_or_default(j, "playerWatched", r.playerWatched);
    r.playerFriendly = get_vec4_or_default(j, "playerFriendly", r.playerFriendly);
    r.playerLocal = get_vec4_or_default(j, "playerLocal", r.playerLocal);
    r.playerCorpse = get_vec4_or_default(j, "playerCorpse", r.playerCorpse);
    r.playerGroupLine = get_vec4_or_default(j, "playerGroupLine", r.playerGroupLine);
    r.grenades = get_vec4_or_default(j, "grenades", r.grenades);
    r.exfils = get_vec4_or_default(j, "exfils", r.exfils);
    r.questMarker = get_vec4_or_default(j, "questMarker", r.questMarker);
    r.crosshair = get_vec4_or_default(j, "crosshair", r.crosshair);
    r.fovCircle = get_vec4_or_default(j, "fovCircle", r.fovCircle);
    r.questColour = get_vec4_or_default(j, "questColour", r.questColour);
    r.wishListColour = get_vec4_or_default(j, "wishListColour", r.wishListColour);
    r.valueLootColour = get_vec4_or_default(j, "valueLootColour", r.valueLootColour);
}

void to_json(nlohmann::json& j, const DxFontSettings& f)
{
    j = nlohmann::json{
        { "name", ConfigWideToUtf8(f.name) },
        { "size", f.size },
        { "bold", f.bold },
        { "italic", f.italic }
    };
}

void from_json(const nlohmann::json& j, DxFontSettings& f)
{
    const std::string fontName = j.value("name", std::string("Arial"));

    f.name = ConfigUtf8ToWide(fontName);
    f.size = j.value("size", f.size);
    f.bold = j.value("bold", f.bold);

    f.italic = false;
}

void to_json(nlohmann::json& j, const DxWindowConfig& f)
{
    j = nlohmann::json{
        { "autoStart", f.autoStart },

        { "monitorIndex", f.monitorIndex },
        { "useMonitorSize", f.useMonitorSize },

        { "fullscreen", f.fullscreen },
        { "borderless", f.borderless },

        { "windowWidth", f.windowWidth },
        { "windowHeight", f.windowHeight },

        { "topMost", f.topMost },
        { "showInTaskbar", f.showInTaskbar },

        { "transparentBackground", f.transparentBackground },
        { "backgroundColour", Vec4ToJson(f.backgroundColour) },

        { "useVSync", f.useVSync },
        { "useMonitorRefreshRate", f.useMonitorRefreshRate },
        { "maxFPS", f.maxFPS },

        { "antiAliasing", f.antiAliasing },

        { "useDpiScale", f.useDpiScale },
        { "renderScale", f.renderScale },

        { "defaultFont", f.defaultFont }
    };
}

void from_json(const nlohmann::json& j, DxWindowConfig& f)
{
    f.autoStart = j.value("autoStart", f.autoStart);

    f.monitorIndex = j.value("monitorIndex", f.monitorIndex);
    f.useMonitorSize = j.value("useMonitorSize", f.useMonitorSize);

    f.fullscreen = j.value("fullscreen", f.fullscreen);
    f.borderless = j.value("borderless", f.borderless);

    f.windowWidth = j.value("windowWidth", f.windowWidth);
    f.windowHeight = j.value("windowHeight", f.windowHeight);

    f.topMost = j.value("topMost", f.topMost);
    f.showInTaskbar = j.value("showInTaskbar", f.showInTaskbar);

    f.transparentBackground = j.value("transparentBackground", f.transparentBackground);

    if (j.contains("backgroundColour"))
        f.backgroundColour = JsonToVec4(j.at("backgroundColour"), f.backgroundColour);

    f.useVSync = j.value("useVSync", f.useVSync);
    f.useMonitorRefreshRate = j.value("useMonitorRefreshRate", f.useMonitorRefreshRate);
    f.maxFPS = j.value("maxFPS", f.maxFPS);

    f.antiAliasing = j.value("antiAliasing", f.antiAliasing);

    f.useDpiScale = j.value("useDpiScale", f.useDpiScale);
    f.renderScale = j.value("renderScale", f.renderScale);

    if (j.contains("defaultFont"))
        f.defaultFont = j.at("defaultFont").get<DxFontSettings>();

    f.monitorIndex = std::max(0, f.monitorIndex);

    f.windowWidth = std::max(320, f.windowWidth);
    f.windowHeight = std::max(240, f.windowHeight);

    f.maxFPS = std::clamp(f.maxFPS, 30, 1000);
    f.renderScale = std::clamp(f.renderScale, 0.05f, 5.0f);

    if (f.fullscreen)
    {
        f.borderless = true;
        f.useMonitorSize = true;
    }

    f.defaultFont.italic = false;
}

// Custom serialization for radarGlobals
void to_json(nlohmann::json& j, const radarGlobals& r) {
    j = nlohmann::json{
        {"drawPlayers", r.drawPlayers},
        {"drawLoot", r.drawLoot},
        {"drawGrenades", r.drawGrenades},
        {"drawExfils", r.drawExfils},
        {"drawQuestHelper", r.drawQuestHelper},
        {"localAimLine", r.localAimLine},
        {"friendAimLine", r.friendAimLine},
        {"enemyAimLine", r.enemyAimLine},
        {"getPlayerEquip", r.getPlayerEquip},
        {"getPlayerStats", r.getPlayerStats}

    };
}

void from_json(const nlohmann::json& j, radarGlobals& r) {
    r.drawPlayers = j.value("drawPlayers", r.drawPlayers);
    r.drawLoot = j.value("drawLoot", r.drawLoot);
    r.drawGrenades = j.value("drawGrenades", r.drawGrenades);
    r.drawExfils = j.value("drawExfils", r.drawExfils);
    r.drawQuestHelper = j.value("drawQuestHelper", r.drawQuestHelper);
    r.localAimLine = j.value("localAimLine", r.localAimLine);
    r.friendAimLine = j.value("friendAimLine", r.friendAimLine);
    r.enemyAimLine = j.value("enemyAimLine", r.enemyAimLine);
    r.getPlayerEquip = j.value("getPlayerEquip", r.getPlayerEquip);
    r.getPlayerStats = j.value("getPlayerStats", r.getPlayerStats);
}


// Custom serialization for espGlobals
void to_json(nlohmann::json& j, const espGlobals& e) {
    j = nlohmann::json{
        {"espEnabled", e.espEnabled},
        {"drawPlayers", e.drawPlayers},
        {"drawPlayerDist", e.drawPlayerDist},
        {"drawGrenades", e.drawGrenades},
        {"drawGrenadesDist", e.drawGrenadesDist},
        {"drawLoot", e.drawLoot},
        {"drawLootDist", e.drawLootDist},
        {"drawQuestHelper", e.drawQuestHelper},
        {"drawCorpse", e.drawCorpse},
        {"drawCorpseDist", e.drawCorpseDist},
        {"drawBoxPlayers", e.drawBoxPlayers},
        {"drawHealthPlayers", e.drawHealthPlayers},
        {"gameRes", e.gameRes},
        {"gameResInt", e.gameResInt},
        {"drawSkeletons", e.drawSkeletons},
        {"skeletonsOnlyClosest", e.skeletonsOnlyClosest},
        {"drawCrosshair", e.drawCrosshair},
        {"headDotSize", e.headDotSize},
        {"drawHeadDot", e.drawHeadDot},
        {"drawFireportLine", e.drawFireportLine},
        {"drawExfilDist", e.drawExfilDist},
        {"drawExfil", e.drawExfil}
    };
}

void from_json(const nlohmann::json& j, espGlobals& e) {
    e.espEnabled = j.value("espEnabled", e.espEnabled);
    e.drawPlayers = j.value("drawPlayers", e.drawPlayers);
    e.drawPlayerDist = j.value("drawPlayerDist", e.drawPlayerDist);
    e.drawGrenades = j.value("drawGrenades", e.drawGrenades);
    e.drawGrenadesDist = j.value("drawGrenadesDist", e.drawGrenadesDist);
    e.drawLoot = j.value("drawLoot", e.drawLoot);
    e.drawLootDist = j.value("drawLootDist", e.drawLootDist);
    e.drawQuestHelper = j.value("drawQuestHelper", e.drawQuestHelper);
    e.drawCorpse = j.value("drawCorpse", e.drawCorpse);
    e.drawCorpseDist = j.value("drawCorpseDist", e.drawCorpseDist);
    e.drawBoxPlayers = j.value("drawBoxPlayers", e.drawBoxPlayers);
    e.drawHealthPlayers = j.value("drawHealthPlayers", e.drawHealthPlayers);
    e.gameRes = get_vec2_or_default(j, "gameRes", e.gameRes);
    e.gameResInt = j.value("gameResInt", e.gameResInt);
    e.drawSkeletons = j.value("drawSkeletons", e.drawSkeletons);
    e.skeletonsOnlyClosest = j.value("skeletonsOnlyClosest", e.skeletonsOnlyClosest);
    e.drawCrosshair = j.value("drawCrosshair", e.drawCrosshair);
    e.drawHeadDot = j.value("drawHeadDot", e.drawHeadDot);
    e.headDotSize = j.value("headDotSize", e.headDotSize);
    e.drawFireportLine = j.value("drawFireportLine", e.drawFireportLine);
    e.drawExfilDist = j.value("drawExfilDist", e.drawExfilDist);
    e.drawExfil = j.value("drawExfil", e.drawExfil);
}

// Custom serialization for aimGlobals
void to_json(nlohmann::json& j, const aimGlobals& a) {
    j = nlohmann::json{
        {"aimEnabled", a.aimEnabled},
        {"aimFOV", a.aimFOV},
        {"aimDistance", a.aimDistance},
        {"aiBone", a.aiBone},
        {"pmcBone", a.pmcBone},
        {"targetLock", a.targetLock},
        {"targetMode", a.targetMode},
        {"aimSmooth", a.aimSmooth},
        {"aimReference", a.aimReference},
        {"showAimFovRing", a.showAimFovRing},
        {"fireportLineLengthM", a.fireportLineLengthM}
    };
}

void from_json(const nlohmann::json& j, aimGlobals& a) {
    a.aimEnabled = j.value("aimEnabled", a.aimEnabled);
    a.aimFOV = j.value("aimFOV", a.aimFOV);
    a.aimDistance = j.value("aimDistance", a.aimDistance);
    a.aiBone = j.value("aiBone", a.aiBone);
    a.pmcBone = j.value("pmcBone", a.pmcBone);
    a.targetLock = j.value("targetLock", a.targetLock);
    a.targetMode = j.value("targetMode", a.targetMode);
    a.aimSmooth = j.value("aimSmooth", a.aimSmooth);
    a.aimReference = j.value("aimReference", a.aimReference);
    a.showAimFovRing = j.value("showAimFovRing", a.showAimFovRing);
    a.fireportLineLengthM = j.value("fireportLineLengthM", a.fireportLineLengthM);
}



// Custom serialization for keyGlobals
void to_json(nlohmann::json& j, const keyGlobals& k) {
    j = nlohmann::json{
        {"aimKey", k.aimKey},
        {"toggleFollow", k.toggleFollow},
        {"battleMode", k.battleMode}
    };
}

void from_json(const nlohmann::json& j, keyGlobals& k) {
    k.aimKey = j.value("aimKey", k.aimKey);
    k.toggleFollow = j.value("toggleFollow", k.toggleFollow);
    k.battleMode = j.value("battleMode", k.battleMode);
}

//custom serialization for Loot Filters
void to_json(nlohmann::json& j, const lootFilterItems& k) {
    j = nlohmann::json{
        {"bsgid", k.bsgid},
        {"name", k.name},
        {"shortName", k.shortName},
        {"traderPrice", k.traderPrice},
        {"marketPrice", k.marketPrice}
    };
}

void from_json(const nlohmann::json& j, lootFilterItems& k) {
    k.bsgid = j.value("bsgid", "");
    k.name = j.value("name", "");
    k.shortName = j.value("shortName", "");
    k.traderPrice = j.value("traderPrice", 0);
    k.marketPrice = j.value("marketPrice", 0);
}

void to_json(nlohmann::json& j, const LootFilters& k) {
    j = nlohmann::json{
        {"id", k.id},
        {"filterName", k.filterName},
        {"filterColour", k.filterColour},
        {"active", k.active},
        {"lootItems", k.lootItems}
    };
}

void from_json(const nlohmann::json& j, LootFilters& k) {
    j.at("id").get_to(k.id);
    j.at("filterName").get_to(k.filterName);
    j.at("filterColour").get_to(k.filterColour);
    j.at("active").get_to(k.active);
    j.at("lootItems").get_to(k.lootItems);
}


void to_json(nlohmann::json& j, const lootGlobals& k) {
    j = nlohmann::json{
        {"enableQuestLoot", k.enableQuestLoot},
        {"enableValueLoot", k.enableValueLoot},
        {"enableWishListLoot", k.enableWishListLoot},
        {"valueLootFrom", k.valueLootFrom}
    };
}

void from_json(const nlohmann::json& j, lootGlobals& k) {
    j.at("enableQuestLoot").get_to(k.enableQuestLoot);
    j.at("enableValueLoot").get_to(k.enableValueLoot);
    j.at("enableWishListLoot").get_to(k.enableWishListLoot);
    j.at("valueLootFrom").get_to(k.valueLootFrom);
}

void to_json(nlohmann::json& j, const MakcuConfig& k)
{
    j = nlohmann::json{
        { "comPort", std::string(k.comPort) },
        { "connectOnStartup", k.connectOnStartup }
    };
}

void from_json(const nlohmann::json& j, MakcuConfig& k)
{
    
    k.comPort[0] = '\0';
    k.connectOnStartup = false;

    if (const auto it = j.find("comPort");
        it != j.end() && it->is_string())
    {
        const std::string port = it->get<std::string>();

        std::snprintf(
            k.comPort,
            sizeof(k.comPort),
            "%s",
            port.c_str()
        );
    }

    if (const auto it = j.find("connectOnStartup");
        it != j.end() && it->is_boolean())
    {
        k.connectOnStartup = it->get<bool>();
    }
}

// ConfigManager methods
ConfigManager::ConfigManager(const std::string& configFilename, const std::string& lootFilterFilename)
    : filename_(configFilename), filename_lootFilter(lootFilterFilename) {
}


bool ConfigManager::LoadLootFilterConfig() {
    fs::path exeDir = fs::current_path();
    fs::path configDir = exeDir / "configs";
    fs::path configFile = configDir / filename_lootFilter;

    if (!fs::exists(configDir)) {
        return false;
    }

    std::ifstream file(configFile);
    if (!file.is_open()) {
        return false;
    }

    nlohmann::json j;
    file >> j;

    try {
        if (j.contains("lootFilters")) {
            lootFilters = j.at("lootFilters").get<std::vector<LootFilters>>();
        }


        return true;
    }
    catch (const nlohmann::json::exception& e) {
        return false;
    }
}

bool ConfigManager::LoadConfig()
{
    const fs::path exeDir = fs::current_path();
    const fs::path configDir = exeDir / "configs";
    const fs::path configFile = configDir / filename_;

    if (!fs::exists(configDir))
        return false;

    std::ifstream file(configFile);

    if (!file.is_open())
        return false;

    try
    {
        nlohmann::json j;
        file >> j;

        if (j.contains("app") && j["app"].is_object())
        {
            app_ = j.at("app").get<globals>();
        }

        if (j.contains("fuser") && j["fuser"].is_object())
        {
            fuser_ = j.at("fuser").get<DxWindowConfig>();

            g_DxWindow.SetConfig(fuser_);

            if (fuser_.autoStart)
            {
                g_DxWindow.Init(fuser_);
                g_DxWindow.Start();
            }
        }

        if (j.contains("radarGlobals") &&
            j["radarGlobals"].is_object())
        {
            radar_ =
                j.at("radarGlobals").get<radarGlobals>();
        }

        if (j.contains("espGlobals") &&
            j["espGlobals"].is_object())
        {
            esp_ =
                j.at("espGlobals").get<espGlobals>();
        }

        if (j.contains("aimGlobals") &&
            j["aimGlobals"].is_object())
        {
            aim_ =
                j.at("aimGlobals").get<aimGlobals>();
        }

        if (j.contains("coloursGlobals") &&
            j["coloursGlobals"].is_object())
        {
            colours_ =
                j.at("coloursGlobals").get<coloursGlobals>();
        }

        if (j.contains("keyGlobals") &&
            j["keyGlobals"].is_object())
        {
            keys_ =
                j.at("keyGlobals").get<keyGlobals>();
        }

        if (j.contains("lootGlobals") &&
            j["lootGlobals"].is_object())
        {
            loot_ =
                j.at("lootGlobals").get<lootGlobals>();
        }

        if (j.contains("makcu") &&
            j["makcu"].is_object())
        {
            makcu_ =
                j.at("makcu").get<MakcuConfig>();

            makcuConfig = makcu_;
        }
        else
        {
            makcu_ = MakcuConfig{};
            makcuConfig = makcu_;
        }

        ConnectMakcuOnStartup();

        return true;
    }
    catch (const nlohmann::json::exception&)
    {
        return false;
    }
    catch (const std::exception&)
    {
        return false;
    }
}

bool ConfigManager::SaveConfig()
{
    fs::path exeDir = fs::current_path();
    fs::path configDir = exeDir / "configs";
    fs::path configFile = configDir / filename_;

    if (!fs::exists(configDir))
    {
        if (!fs::create_directory(configDir))
            return false;
    }

    fuser_ = g_DxWindow.GetConfig();
    makcu_ = makcuConfig;

    nlohmann::json j;

    j["app"] = app_;
    j["fuser"] = fuser_;
    j["radarGlobals"] = radar_;
    j["espGlobals"] = esp_;
    j["aimGlobals"] = aim_;
    j["coloursGlobals"] = colours_;
    j["keyGlobals"] = keys_;
    j["lootGlobals"] = loot_;
    j["makcu"] = makcu_;

    std::ofstream file(configFile);
    if (!file.is_open())
        return false;

    file << j.dump(4);
    return true;
}

bool ConfigManager::SaveLootFilterConfig() {
    fs::path exeDir = fs::current_path();
    fs::path configDir = exeDir / "configs";
    fs::path configFile = configDir / filename_lootFilter;

    if (!fs::exists(configDir)) {
        fs::create_directory(configDir);
    }

    nlohmann::json j;
    j["lootFilters"] = lootFilters;

    std::ofstream file(configFile);
    if (!file.is_open()) {
        return false;
    }

    file << j.dump(4);
    return true;
}

bool ConfigManager::refreshFilterLootPrices() {

    bool updated = false;
    int update_cout = 0;

    if (marketList.size() == 0 && lootFilters.size() == 0)
        return false;

    for (auto& lootFiltertmp : lootFilters)
    {
        if (lootFiltertmp.lootItems.size() > 0)
        {
            for (auto& lootInFilter : lootFiltertmp.lootItems)
            {
                for (auto& marketitem : marketList)
                {
                    if (marketitem.bsgid == lootInFilter.bsgid)
                    {
                        lootInFilter.marketPrice = marketitem.marketPrice;
                        lootInFilter.traderPrice = marketitem.traderPrice;

                        update_cout++;
                        updated = true;
                        break; 
                    }
                }
            }
        }
    }

    if (updated)
    {
        
        if (this->SaveLootFilterConfig())
            LOGS.logInfo("[CONFIG] updated " + std::to_string(update_cout) + " item prices in memory/json");
        else
            LOGS.logWarn("[CONFIG][UPDATE] failed to update market prices in lootFilters.json");
    }


}