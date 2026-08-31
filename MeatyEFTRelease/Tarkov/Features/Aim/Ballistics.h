#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <glm/glm.hpp>
#include <iterator>

struct BallisticsInfo
{
    float bulletSpeed = 0.0f;
    float bulletMassGrams = 0.0f;
    float bulletDiameterMillimeters = 0.0f;
    float ballisticCoefficient = 0.0f;

    [[nodiscard]] bool IsValid() const noexcept
    {
        return
            std::isfinite(bulletSpeed) &&
            std::isfinite(bulletMassGrams) &&
            std::isfinite(bulletDiameterMillimeters) &&
            std::isfinite(ballisticCoefficient) &&
            bulletSpeed > 1.0f && bulletSpeed < 2500.0f &&
            bulletMassGrams > 0.0f && bulletMassGrams < 2000.0f &&
            bulletDiameterMillimeters > 0.0f &&
            bulletDiameterMillimeters <= 100.0f &&
            ballisticCoefficient > 0.0f &&
            ballisticCoefficient <= 3.0f;
    }
};

struct BallisticSimulationResult
{
    float dropCompensation = 0.0f;
    float travelTime = 0.0f;
    bool valid = false;
};

class BallisticsCalculator
{
public:
    [[nodiscard]] static BallisticSimulationResult Simulate(
        const glm::vec3& startPosition,
        const glm::vec3& endPosition,
        const BallisticsInfo& ballistics) noexcept
    {
        if (!ballistics.IsValid())
            return {};

        const float shotDistance = glm::distance(
            startPosition,
            endPosition);

        if (!std::isfinite(shotDistance) || shotDistance <= 0.01f)
            return {};

        constexpr int kMaximumIterations = 1300;
        constexpr float kSimulationTimeStep = 0.01f;
        constexpr float kGravity = -9.81f;

        const float bulletMassKilograms = ballistics.bulletMassGrams / 1000.0f;
        const float doubleMass = bulletMassKilograms * 2.0f;
        const float bulletDiameterMetres = ballistics.bulletDiameterMillimeters / 1000.0f;
        const float dragScale = bulletMassKilograms * 0.0014223f / (bulletDiameterMetres * bulletDiameterMetres * ballistics.ballisticCoefficient);
        const float frontalArea = bulletDiameterMetres * bulletDiameterMetres * 3.1415927f / 4.0f;
        const float airDensityArea = 1.2f * frontalArea;

        float time = 0.0f;
        float previousTravelTime = 0.0f;
        glm::vec3 previousPosition{};
        glm::vec3 previousVelocity{0.0f, 0.0f, ballistics.bulletSpeed};

        for (int iteration = 1; iteration < kMaximumIterations; ++iteration)
        {
            const float speed = glm::length(previousVelocity);

            if (!std::isfinite(speed) || speed <= 0.01f)
                return {};

            const float dragCoefficient = CalculateG1DragCoefficient(speed) * dragScale;
            const glm::vec3 acceleration = glm::vec3(0.0f, kGravity, 0.0f) + airDensityArea * -dragCoefficient * speed * speed / doubleMass * glm::normalize(previousVelocity);

            const glm::vec3 currentPosition = previousPosition + previousVelocity * kSimulationTimeStep + acceleration * (0.5f * kSimulationTimeStep * kSimulationTimeStep);
            const glm::vec3 currentVelocity = previousVelocity + acceleration * kSimulationTimeStep;
            const float currentDistance = glm::length(currentPosition);

            if (!std::isfinite(currentDistance))
                return {};

            if (currentDistance >= shotDistance)
            {
                const float segmentDistance = currentDistance - glm::length(previousPosition);
                const float lerp = segmentDistance > 0.0001f
                    ? std::clamp((shotDistance - glm::length(previousPosition)) / segmentDistance,
                        0.0f,
                        1.0f)
                    : 1.0f;
                const glm::vec3 impactPosition = previousPosition + (currentPosition - previousPosition) * lerp;
                const float travelTime = previousTravelTime + (time + kSimulationTimeStep - previousTravelTime) * lerp;

                if (!std::isfinite(impactPosition.y) || !std::isfinite(travelTime) || travelTime <= 0.0f)
                {
                    return {};
                }

                return { std::fabs(impactPosition.y), travelTime, true };
            }

            time += kSimulationTimeStep;
            previousTravelTime = time;
            previousPosition = currentPosition;
            previousVelocity = currentVelocity;
        }

        return {};
    }

private:
    struct G1Entry
    {
        float mach;
        float coefficient;
    };

    [[nodiscard]] static float CalculateG1DragCoefficient(
        float velocity) noexcept
    {
        static constexpr G1Entry kG1Coefficients[] =
        {
            {0.0f, 0.2629f}, {0.05f, 0.2558f}, {0.1f, 0.2487f},
            {0.15f, 0.2413f}, {0.2f, 0.2344f}, {0.25f, 0.2278f},
            {0.3f, 0.2214f}, {0.35f, 0.2155f}, {0.4f, 0.2104f},
            {0.45f, 0.2061f}, {0.5f, 0.2032f}, {0.55f, 0.2020f},
            {0.6f, 0.2034f}, {0.7f, 0.2165f}, {0.725f, 0.2230f},
            {0.75f, 0.2313f}, {0.775f, 0.2417f}, {0.8f, 0.2546f},
            {0.825f, 0.2706f}, {0.85f, 0.2901f}, {0.875f, 0.3136f},
            {0.9f, 0.3415f}, {0.925f, 0.3734f}, {0.95f, 0.4084f},
            {0.975f, 0.4448f}, {1.0f, 0.4805f}, {1.025f, 0.5136f},
            {1.05f, 0.5427f}, {1.075f, 0.5677f}, {1.1f, 0.5883f},
            {1.125f, 0.6053f}, {1.15f, 0.6191f}, {1.2f, 0.6393f},
            {1.25f, 0.6518f}, {1.3f, 0.6589f}, {1.35f, 0.6621f},
            {1.4f, 0.6625f}, {1.45f, 0.6607f}, {1.5f, 0.6573f},
            {1.55f, 0.6528f}, {1.6f, 0.6474f}, {1.65f, 0.6413f},
            {1.7f, 0.6347f}, {1.75f, 0.6280f}, {1.8f, 0.6210f},
            {1.85f, 0.6141f}, {1.9f, 0.6072f}, {1.95f, 0.6003f},
            {2.0f, 0.5934f}, {2.05f, 0.5867f}, {2.1f, 0.5804f},
            {2.15f, 0.5743f}, {2.2f, 0.5685f}, {2.25f, 0.5630f},
            {2.3f, 0.5577f}, {2.35f, 0.5527f}, {2.4f, 0.5481f},
            {2.45f, 0.5438f}, {2.5f, 0.5397f}, {2.6f, 0.5325f},
            {2.7f, 0.5264f}, {2.8f, 0.5211f}, {2.9f, 0.5168f},
            {3.0f, 0.5133f}, {3.1f, 0.5105f}, {3.2f, 0.5084f},
            {3.3f, 0.5067f}, {3.4f, 0.5054f}, {3.5f, 0.5040f},
            {3.6f, 0.5030f}, {3.7f, 0.5022f}, {3.8f, 0.5016f},
            {3.9f, 0.5010f}, {4.0f, 0.5006f}, {4.2f, 0.4998f},
            {4.4f, 0.4995f}, {4.6f, 0.4992f}, {4.8f, 0.4990f},
            {5.0f, 0.4988f}
        };

        constexpr float kSpeedOfSound = 343.0f;
        const float mach = velocity / kSpeedOfSound;

        if (!std::isfinite(mach) || mach <= 0.0f)
            return 0.0f;

        const G1Entry* upper = std::lower_bound(
            std::begin(kG1Coefficients),
            std::end(kG1Coefficients),
            mach,
            [](const G1Entry& entry, float targetMach)
            {
                return entry.mach < targetMach;
            });

        if (upper == std::begin(kG1Coefficients))
            return upper->coefficient;

        if (upper == std::end(kG1Coefficients))
            return std::prev(upper)->coefficient;

        const G1Entry& lower = *std::prev(upper);
        const float range = upper->mach - lower.mach;
        const float lerp = range > 0.0f
            ? (mach - lower.mach) / range
            : 0.0f;

        return lower.coefficient + (upper->coefficient - lower.coefficient) * lerp;
    }
};
