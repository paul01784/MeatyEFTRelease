#pragma once

#include <array>
#include <cstdint>
#include <string_view>

#include <glm/glm.hpp>

enum class TransitId : std::uint16_t
{
    Unknown = 0,
    GroundZeroToStreets = 1,
    StreetsToGroundZero = 3,
    StreetsToInterchange = 4,
    StreetsToLaboratoryDark = 5,
    InterchangeToCustoms = 6,
    InterchangeToStreets = 7,
    LaboratoryToStreets = 8,
    CustomsToReserve = 9,
    CustomsToFactory = 10,
    CustomsToInterchange = 11,
    FactoryToWoods = 12,
    FactoryToCustoms = 13,
    FactoryToLaboratoryDark = 14,
    WoodsToFactory = 15,
    WoodsToReserve = 16,
    WoodsToLighthouse = 17,
    ReserveToCustoms = 18,
    ReserveToWoods = 19,
    ReserveToLighthouse = 20,
    LighthouseToShoreline = 21,
    LighthouseToReserve = 22,
    LighthouseToWoods = 23,
    ShorelineToLighthouse = 24,
    ShorelineToTerminal = 25,
    WoodsToCustoms = 41,
    CustomsToShoreline = 42,
    ShorelineToLabyrinth = 43,
};

struct TransitDefinition
{
    TransitId id = TransitId::Unknown;
    std::string_view sourceMapId;
    std::string_view destinationName;
    glm::vec3 worldPosition{};
};

inline const std::array<TransitDefinition, 31> kTransitDefinitions =
{
    TransitDefinition{ TransitId::GroundZeroToStreets, "Sandbox", "Streets", { 222.829117f, 15.993f, 65.72f } },
    TransitDefinition{ TransitId::GroundZeroToStreets, "Sandbox_high", "Streets", { 222.829117f, 15.993f, 65.72f } },
    TransitDefinition{ TransitId::StreetsToGroundZero, "TarkovStreets", "Ground Zero", { -260.05f, 2.18500137f, 103.645729f } },
    TransitDefinition{ TransitId::StreetsToInterchange, "TarkovStreets", "Interchange", { 286.670776f, 3.39350128f, 505.539978f } },
    TransitDefinition{ TransitId::StreetsToLaboratoryDark, "TarkovStreets", "Laboratory", { 206.955f, -8.382f, 82.194f } },
    TransitDefinition{ TransitId::InterchangeToCustoms, "Interchange", "Customs", { 274.3f, 23.2799988f, 395.93f } },
    TransitDefinition{ TransitId::InterchangeToStreets, "Interchange", "Streets", { 263.1f, 24.1f, -444.4f } },
    TransitDefinition{ TransitId::LaboratoryToStreets, "laboratory", "Streets", { -169.079956f, 1.32f, -420.878052f } },
    TransitDefinition{ TransitId::CustomsToReserve, "bigmap", "Reserve", { 651.7f, 1.99f, 124.6f } },
    TransitDefinition{ TransitId::CustomsToFactory, "bigmap", "Factory", { 354.31f, 2.31f, -190.1f } },
    TransitDefinition{ TransitId::CustomsToInterchange, "bigmap", "Interchange", { -335.24f, 2.11f, -205.68f } },
    TransitDefinition{ TransitId::CustomsToShoreline, "bigmap", "Shoreline", { 23.67f, -1.3999995f, 139.53f } },
    TransitDefinition{ TransitId::FactoryToWoods, "factory4_day", "Woods", { 23.7f, 0.84f, 61.61f } },
    TransitDefinition{ TransitId::FactoryToCustoms, "factory4_day", "Customs", { 18.51f, -0.423f, -48.33f } },
    TransitDefinition{ TransitId::FactoryToLaboratoryDark, "factory4_day", "Laboratory", { -26.539f, -4.085f, -42.013f } },
    TransitDefinition{ TransitId::FactoryToWoods, "factory4_night", "Woods", { 23.7f, 0.84f, 61.61f } },
    TransitDefinition{ TransitId::FactoryToCustoms, "factory4_night", "Customs", { 18.51f, -0.423f, -48.33f } },
    TransitDefinition{ TransitId::FactoryToLaboratoryDark, "factory4_night", "Laboratory", { -26.539f, -4.085f, -42.013f } },
    TransitDefinition{ TransitId::WoodsToFactory, "Woods", "Factory", { -362.45f, 0.821269155f, 360.7704f } },
    TransitDefinition{ TransitId::WoodsToReserve, "Woods", "Reserve", { 246.06f, -10.22f, 369.9f } },
    TransitDefinition{ TransitId::WoodsToLighthouse, "Woods", "Lighthouse", { 494.34f, -16.77f, 345.37f } },
    TransitDefinition{ TransitId::WoodsToCustoms, "Woods", "Customs", { -153.05f, 1.07f, 402.13f } },
    TransitDefinition{ TransitId::ReserveToCustoms, "RezervBase", "Customs", { -196.729477f, -4.544204f, -117.808853f } },
    TransitDefinition{ TransitId::ReserveToWoods, "RezervBase", "Woods", { 216.860535f, -6.344204f, -201.048859f } },
    TransitDefinition{ TransitId::ReserveToLighthouse, "RezervBase", "Lighthouse", { 238.800537f, -6.25420427f, -128.048859f } },
    TransitDefinition{ TransitId::LighthouseToShoreline, "Lighthouse", "Shoreline", { -338.6f, 17.5f, -168.9f } },
    TransitDefinition{ TransitId::LighthouseToReserve, "Lighthouse", "Reserve", { -294.6f, 15.2591143f, -780.1f } },
    TransitDefinition{ TransitId::LighthouseToWoods, "Lighthouse", "Woods", { 106.3f, 6.74f, -958.1f } },
    TransitDefinition{ TransitId::ShorelineToLighthouse, "Shoreline", "Lighthouse", { 417.2f, -56.1f, -217.5f } },
    TransitDefinition{ TransitId::ShorelineToTerminal, "Shoreline", "Terminal", { -965.82f, -57.52f, 364.49f } },
    TransitDefinition{ TransitId::ShorelineToLabyrinth, "Shoreline", "Labyrinth", { -197.958f, -9.867f, -81.085f } },
};
