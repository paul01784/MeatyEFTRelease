#include "../MeatyEFTRelease/UI/BodySilhouette.h"
#include <cassert>
#include <iostream>
#include <limits>

using namespace BodySilhouette;
static Pose standing()
{
    Pose pose{};
    const auto put = [&](int i, float x, float y) { pose[i] = {{x, y}, true}; };
    put(Head, 100, 50); put(Neck, 100, 68); put(Spine, 100, 100); put(Pelvis, 100, 150);
    for (int side = 0; side < 2; ++side)
    {
        const float sign = side ? 1.f : -1.f;
        put(side ? RShoulder : LShoulder, 100 + sign * 24, 78);
        put(side ? RElbow : LElbow, 100 + sign * 38, 108);
        put(side ? RForearm : LForearm, 100 + sign * 38, 115);
        put(side ? RPalm : LPalm, 100 + sign * 35, 138);
        put(side ? RThigh : LThigh, 100 + sign * 13, 160);
        put(side ? RKnee : LKnee, 100 + sign * 15, 205);
        put(side ? RFoot : LFoot, 100 + sign * 16, 250);
        put(side ? RToe : LToe, 100 + sign * 23, 257);
    }
    return pose;
}

static void check(const Pose& pose)
{
    for (auto style : {Style::Plain, Style::Soldier})
    {
        auto shapes = build(pose, style);
        assert(!shapes.empty() && shapes.size() <= 128);
        for (const auto& shape : shapes)
        {
            assert(shape.size() >= 3 && shape.size() <= 64);
            for (auto p : shape) assert(finite(p));
            // Both rendering backends expect consistently wound convex parts.
            for (size_t i = 0; i < shape.size(); ++i)
            {
                auto a = sub(shape[(i+1)%shape.size()], shape[i]);
                auto b = sub(shape[(i+2)%shape.size()], shape[(i+1)%shape.size()]);
                assert(a.x*b.y - a.y*b.x >= -0.01f);
            }
        }
    }
}

int main()
{
    auto pose = standing();
    check(pose);
    auto side = pose;
    side[LShoulder].p.x = 99; side[RShoulder].p.x = 101;
    side[LThigh].p.x = 98; side[RThigh].p.x = 102;
    check(side);
    // Pose degradation preserves a usable fallback without inventing coordinates.
    for (int i = Base+1; i < Count; ++i) pose[i].valid = false;
    check(pose);
    pose[LPalm].p.x = std::numeric_limits<float>::quiet_NaN();
    check(pose);
    pose[Head].valid = false;
    assert(build(pose).empty());
    assert(build(Pose{}).empty());
    assert(classifyHeadwear("Combat helmet") == Headwear::Helmet);
    assert(classifyHeadwear("Baseball cap") == Headwear::Cap);
    std::cout << "PASS: front/profile geometry, optional-joint fallback, invalid coordinates, headwear\n";
}
