#pragma once


#include <glm/glm.hpp>
#include "../external/nlohmann/json.hpp"
#include "render.h" 
#include "globals.h" 
#include "../Core/Utilities.h"
#include "../Tarkov/GameWorld/Loot/Loot.h"
#include "DxRenderWindow.h"
#include "aimview.h"
#include "makcu.h"
#include "../Tarkov/Features/Visibility/AtlasVisibility.h"


void to_json(nlohmann::json& j, const WindowsKey& k);
void from_json(const nlohmann::json& j, WindowsKey& k);

void to_json(nlohmann::json& j, const coloursGlobals& c);
void from_json(const nlohmann::json& j, coloursGlobals& c);

void to_json(nlohmann::json& j, const radarGlobals& r);
void from_json(const nlohmann::json& j, radarGlobals& r);

void to_json(nlohmann::json& j, const espGlobals& e);
void from_json(const nlohmann::json& j, espGlobals& e);

void to_json(nlohmann::json& j, const AimViewConfig& a);
void from_json(const nlohmann::json& j, AimViewConfig& a);

void to_json(nlohmann::json& j, const atlasVisibilityGlobals& v);
void from_json(const nlohmann::json& j, atlasVisibilityGlobals& v);

void to_json(nlohmann::json& j, const aimGlobals& a);
void from_json(const nlohmann::json& j, aimGlobals& a);

void to_json(nlohmann::json& j, const keyGlobals& k);
void from_json(const nlohmann::json& j, keyGlobals& k);

void to_json(nlohmann::json& j, const globals& k);
void from_json(const nlohmann::json& j, globals& k);

void to_json(nlohmann::json& j, const LootFilters& k);
void from_json(const nlohmann::json& j, LootFilters& k);

void to_json(nlohmann::json& j, const lootFilterItems& k);
void from_json(const nlohmann::json& j, lootFilterItems& k);

void to_json(nlohmann::json& j, const lootGlobals& k);
void from_json(const nlohmann::json& j, lootGlobals& k);

class ConfigManager {
public:
    ConfigManager(const std::string& configFilename, const std::string& lootFilterFilename);

    bool LoadConfig();
    bool LoadLootFilterConfig();
    bool SaveConfig();
    bool SaveLootFilterConfig();

    bool refreshFilterLootPrices();

private:
    std::string filename_;
    std::string filename_lootFilter;
    globals app_;
    DxWindowConfig fuser_;
    AimViewConfig aimView_;
    radarGlobals radar_;
    espGlobals esp_;
    atlasVisibilityGlobals atlasVisibility_;
    aimGlobals aim_;
    coloursGlobals colours_;
    keyGlobals keys_;
    lootGlobals loot_;
    MakcuConfig makcu_;
    memoryGlobals memoryGlobals_;

};

extern ConfigManager configManager;
