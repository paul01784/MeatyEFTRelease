#pragma once

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <glm/glm.hpp>

class PlayerVelocityFilter
{
public:
    bool update(const glm::vec3& sample, std::chrono::steady_clock::time_point now) noexcept
    {
        const float speedSq = glm::dot(sample, sample);
        if (!std::isfinite(sample.x) || !std::isfinite(sample.y) || !std::isfinite(sample.z) || !std::isfinite(speedSq) || speedSq > 225.0f)
            return false;

        if (valid_ && now <= lastSample_)
            return false;

        if (!valid_ || now - lastSample_ > std::chrono::milliseconds(250))
        {
            samples_.fill(sample);
            nextSample_ = 0;
            value_ = sample;
            lastSample_ = now;
            valid_ = true;
            return true;
        }

        samples_[nextSample_] = sample;
        nextSample_ = (nextSample_ + 1) % samples_.size();
        const auto median = [](float a, float b, float c)
        {
            return (std::max)((std::min)(a, b), (std::min)((std::max)(a, b), c));
        };
        const glm::vec3 filtered(
            median(samples_[0].x, samples_[1].x, samples_[2].x),
            median(samples_[0].y, samples_[1].y, samples_[2].y),
            median(samples_[0].z, samples_[1].z, samples_[2].z));

        constexpr float smoothingSeconds = 0.08f;
        const float elapsed = std::chrono::duration<float>(now - lastSample_).count();
        const float blend = std::clamp(-std::expm1(-elapsed / smoothingSeconds), 0.0f, 1.0f);
        value_ += (filtered - value_) * blend;
        if (glm::dot(filtered, filtered) < 0.0001f && glm::dot(value_, value_) < 0.0001f)
            value_ = {};

        lastSample_ = now;
        return true;
    }

    const glm::vec3& value() const noexcept { return value_; }

private:
    std::array<glm::vec3, 3> samples_{};
    std::size_t nextSample_ = 0;
    glm::vec3 value_{};
    std::chrono::steady_clock::time_point lastSample_{};
    bool valid_ = false;
};

class PlayerWorldVelocityTracker
{
public:
    bool update(const glm::vec3& position, std::chrono::steady_clock::time_point now) noexcept
    {
        if (!std::isfinite(position.x) || !std::isfinite(position.y) || !std::isfinite(position.z))
            return false;

        if (!hasPosition_ || now < sampledAt_ || now - sampledAt_ > std::chrono::milliseconds(250))
        {
            position_ = position;
            sampledAt_ = now;
            hasPosition_ = true;
            valid_ = false;
            filter_ = {};
            return false;
        }

        if (now - sampledAt_ < std::chrono::milliseconds(60))
            return false;

        const float elapsed = std::chrono::duration<float>(now - sampledAt_).count();
        glm::vec3 velocity = (position - position_) / elapsed;
        position_ = position;
        sampledAt_ = now;
        const float speedSq = glm::dot(velocity, velocity);
        if (!std::isfinite(speedSq) || speedSq > 225.0f)
        {
            filter_ = {};
            valid_ = false;
            return false;
        }

        if (speedSq < 0.01f)
        {
            velocity = {};
            filter_ = {};
        }
        else if (glm::dot(velocity, filter_.value()) < 0.0f)
        {
            filter_ = {};
        }

        valid_ = filter_.update(velocity, now);
        return valid_;
    }

    bool valid() const noexcept { return valid_; }
    const glm::vec3& value() const noexcept { return filter_.value(); }

private:
    glm::vec3 position_{};
    std::chrono::steady_clock::time_point sampledAt_{};
    PlayerVelocityFilter filter_{};
    bool hasPosition_ = false;
    bool valid_ = false;
};
