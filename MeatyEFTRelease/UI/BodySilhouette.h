#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>
#include <utility>
#include "SilhouetteHeadwear.h"

// Screen-space soldier approximation. Consumes projected bones only.
namespace BodySilhouette
{
    struct Point { float x{}, y{}; };
    struct Joint { Point p{}; bool valid=false; };
    enum Bone { Pelvis, Head, Neck, Spine, LForearm, LPalm, RForearm, RPalm, LThigh, LFoot, RThigh, RFoot,
        Base, LShoulder, RShoulder, LElbow, RElbow, LKnee, RKnee, LToe, RToe, Count };
    using Pose=std::array<Joint,Count>;
    enum class Style { Soldier, Plain };
    struct Polygon : std::vector<Point> { using std::vector<Point>::vector; int region=-1; bool detail=false; };
    using Shapes=std::vector<Polygon>;
    inline Point add(Point a,Point b) { return {a.x+b.x,a.y+b.y}; }
    inline Point sub(Point a,Point b) { return {a.x-b.x,a.y-b.y}; }
    inline Point mul(Point a,float s) { return {a.x*s,a.y*s}; }
    inline float length(Point a) { return std::hypot(a.x,a.y); }
    inline bool finite(Point p) { return std::isfinite(p.x)&&std::isfinite(p.y)&&std::fabs(p.x)<100000&&std::fabs(p.y)<100000; }

    struct OutlineEdge { Point a,b; int region; };
    inline std::vector<OutlineEdge> outline(const Shapes& shapes)
    {
        struct Bounds {float x0,y0,x1,y1;};
        std::vector<Bounds> bounds;
        for(const auto& shape:shapes)
        {
            Bounds b{100000,100000,-100000,-100000};
            for(auto p:shape) {b.x0=(std::min)(b.x0,p.x);b.y0=(std::min)(b.y0,p.y);
                b.x1=(std::max)(b.x1,p.x);b.y1=(std::max)(b.y1,p.y);}
            bounds.push_back(b);
        }
        std::vector<OutlineEdge> result;
        const auto cross=[](Point a,Point b){return a.x*b.y-a.y*b.x;};
        for(size_t s=0;s<shapes.size();++s)
        for(size_t e=0;e<shapes[s].size();++e)
        {
            if(shapes[s].detail) continue;
            const Point a=shapes[s][e],b=shapes[s][(e+1)%shapes[s].size()],d=sub(b,a);
            const float len=length(d); if(len<.001f) continue;
            // Probe just outside this clockwise-on-screen contour so touching
            // parts lose their shared seam without erasing the exterior.
            const Point shift={d.y*.001f/len,-d.x*.001f/len},probe=add(a,shift);
            std::vector<std::pair<float,float>> covered;
            for(size_t other=0;other<shapes.size();++other)
            {
                if(other==s || shapes[other].detail) continue;
                const auto box=bounds[other];
                if((std::max)(a.x,b.x)+.002f<box.x0 || (std::min)(a.x,b.x)-.002f>box.x1 ||
                    (std::max)(a.y,b.y)+.002f<box.y0 || (std::min)(a.y,b.y)-.002f>box.y1) continue;
                float lo=0,hi=1; bool inside=true;
                const auto& p=shapes[other];
                // Earlier shapes own coincident exterior edges, avoiding
                // duplicate strokes along a shared outer contour.
                const Point testPoint=other<s?a:probe;
                for(size_t i=0;i<p.size();++i)
                {
                    const Point edge=sub(p[(i+1)%p.size()],p[i]);
                    const float start=cross(edge,sub(testPoint,p[i])),rate=cross(edge,d);
                    if(std::fabs(rate)<1e-8f) {if(start<0) {inside=false;break;} continue;}
                    const float t=-start/rate;
                    if(rate>0) lo=(std::max)(lo,t); else hi=(std::min)(hi,t);
                    if(lo>=hi) {inside=false;break;}
                }
                if(inside && hi>lo)
                {
                    covered.emplace_back(lo,hi);
                    if(lo==0 && hi==1) break;
                }
            }
            std::sort(covered.begin(),covered.end());
            float at=0;
            const auto emit=[&](float from,float to) {
                if((to-from)*len>.001f) result.push_back({add(a,mul(d,from)),add(a,mul(d,to)),shapes[s].region});
            };
            for(const auto& interval:covered) {if(interval.first>at) emit(at,interval.first);at=(std::max)(at,interval.second);}
            if(at<1) emit(at,1);
        }
        return result;
    }

    // Corner cutting preserves convexity for both renderers' convex fills.
    // Two passes give smooth contours while staying within the 32-point limit.
    inline Polygon rounded(Polygon shape,int passes=2,float cut=.16f)
    {
        while(passes-->0 && shape.size()*2<=32)
        {
            Polygon next; next.region=shape.region; next.detail=shape.detail; next.reserve(shape.size()*2);
            for(size_t i=0;i<shape.size();++i)
            {
                const auto a=shape[i],b=shape[(i+1)%shape.size()];
                next.push_back(add(mul(a,1-cut),mul(b,cut)));
                next.push_back(add(mul(a,cut),mul(b,1-cut)));
            }
            shape=std::move(next);
        }
        return shape;
    }

    inline float fillAlpha(bool enabled,float percent)
    {
        return enabled&&std::isfinite(percent)?(std::clamp)(percent/100.f,0.f,1.f):0.f;
    }

    inline Polygon convexHull(Polygon points)
    {
        std::sort(points.begin(),points.end(),[](Point a,Point b){return a.x==b.x?a.y<b.y:a.x<b.x;});
        points.erase(std::unique(points.begin(),points.end(),[](Point a,Point b){return a.x==b.x&&a.y==b.y;}),points.end());
        if(points.size()<3) return points;
        const auto cross=[](Point a,Point b,Point c){auto u=sub(b,a),v=sub(c,a);return u.x*v.y-u.y*v.x;};
        Polygon hull; hull.region=points.region; hull.detail=points.detail;
        for(auto q:points) {while(hull.size()>1&&cross(hull[hull.size()-2],hull.back(),q)<=0) hull.pop_back();hull.push_back(q);}
        const size_t lower=hull.size();
        for(int i=static_cast<int>(points.size())-2;i>=0;--i) {
            while(hull.size()>lower&&cross(hull[hull.size()-2],hull.back(),points[i])<=0) hull.pop_back();
            hull.push_back(points[i]);
        }
        hull.pop_back();
        return hull;
    }

    // Shared shoulder anchors keep the drawn skeleton inside the same sleeves.
    inline Point shoulderAnchor(Point head, Point hip, Point elbow, int sideIndex)
    {
        const float scale=length(sub(hip,head));
        if(scale<4 || !std::isfinite(scale)) return head;
        const Point down=mul(sub(hip,head),1/scale),side={-down.y,down.x};
        const Point centre=add(head,mul(down,scale*.28f));
        const Point delta=sub(elbow,centre);
        const float lateral=delta.x*side.x+delta.y*side.y;
        const float sign=std::fabs(lateral)>scale*.05f ? (lateral>0?1.f:-1.f) : (sideIndex?1.f:-1.f);
        return add(centre,mul(side,sign*scale*.245f));
    }

    inline std::vector<std::pair<Point,Point>> skeleton(const Pose& p)
    {
        std::vector<std::pair<Point,Point>> lines;
        const auto valid=[&](int i){return p[i].valid&&finite(p[i].p);};
        const auto line=[&](int a,int b){if(valid(a)&&valid(b)) lines.emplace_back(p[a].p,p[b].p);};
        if(valid(Neck)&&valid(Spine)) {line(Head,Neck);line(Neck,Spine);line(Spine,Pelvis);}
        else line(Head,Pelvis);
        for(int s=0;s<2;++s)
        {
            const int shoulder=s?RShoulder:LShoulder;
            const int measuredElbow=s?RElbow:LElbow;
            const int elbow=valid(measuredElbow)?measuredElbow:(s?RForearm:LForearm);
            if(valid(shoulder)) {line(Neck,shoulder);line(shoulder,elbow);}
            else if(valid(Head)&&valid(Pelvis)&&valid(elbow))
                lines.emplace_back(shoulderAnchor(p[Head].p,p[Pelvis].p,p[elbow].p,s),p[elbow].p);
            line(elbow,s?RPalm:LPalm);
            const int thigh=s?RThigh:LThigh,knee=s?RKnee:LKnee,foot=s?RFoot:LFoot;
            line(Pelvis,thigh);
            if(valid(knee)) {line(thigh,knee);line(knee,foot);} else line(thigh,foot);
            line(foot,s?RToe:LToe);
        }
        return lines;
    }

    inline Shapes build(const Pose& input, Style style=Style::Soldier, Headwear headwear=Headwear::Automatic)
    {
        Pose p=input;
        for(auto& j:p) j.valid=j.valid&&finite(j.p);
        if(!p[Head].valid||!p[Pelvis].valid) return {};
        const Point head=p[Head].p,hip=p[Pelvis].p;
        const float scale=length(sub(hip,head));
        if(scale<4||scale>900) return {};
        const Point down=mul(sub(hip,head),1/scale),side={-down.y,down.x};
        const bool soldier=style==Style::Soldier;
        const bool detailed=soldier && scale>=24;
        Shapes shapes;
        shapes.reserve(detailed?32:12);
        const auto polygon=[&](Point origin,Point xAxis,Point yAxis,
            std::initializer_list<Point> contour,int region,bool detail=false)
        {
            Polygon shape;
            shape.region=region;
            shape.detail=detail;
            for(const auto q:contour)
                shape.push_back(add(origin,mul(add(mul(xAxis,q.x),mul(yAxis,q.y)),scale)));
            if(std::all_of(shape.begin(),shape.end(),[](Point q){return finite(q);}))
            {
                // Foreshortened limbs can make a tapered contour concave.
                // Both drawing backends and outline clipping require convex fills.
                shape=convexHull(std::move(shape));
                if(shape.size()>=3) shapes.push_back(rounded(std::move(shape),detail?1:2,detail?.10f:.16f));
            }
        };
        const auto body=[&](std::initializer_list<Point> contour,int region,bool detail=false)
        { polygon(head,side,down,contour,region,detail); };
        const auto segment=[&](Point a,Point b,float upper,float lower,int region)
        {
            const float len=length(sub(b,a));
            if(!finite(a)||!finite(b)||len<1||len>scale*3) return false;
            const Point axis=mul(sub(b,a),1/len),across={-axis.y,axis.x};
            const float extent=len/scale;
            // A fuller proximal muscle, narrowing through the limb to a
            // rounded wrist/ankle. Overlap the joints without moving them.
            const float cap=(std::min)(extent*.08f,(std::max)(upper,lower)*.25f);
            polygon(a,across,axis,{{-upper*.8f,-cap},{upper*.8f,-cap},
                {upper,extent*.18f},{lower,extent*.88f},{lower*.8f,extent+cap},
                {-lower*.8f,extent+cap},{-lower,extent*.88f},{-upper,extent*.18f}},region);
            return true;
        };
        // Use measured joints where available. Existing slots/visibility regions
        // remain stable; optional detail bones never become aim candidates.
        for(int s=0;s<2;++s)
        {
            const int forearm=s?RForearm:LForearm,hand=s?RPalm:LPalm;
            const int measuredElbow=s?RElbow:LElbow,shoulder=s?RShoulder:LShoulder;
            const int elbow=p[measuredElbow].valid?measuredElbow:forearm;
            const int thigh=s?RThigh:LThigh,foot=s?RFoot:LFoot;
            const int knee=s?RKnee:LKnee;
            if(p[elbow].valid)
            {
                const Point sleeve=p[shoulder].valid?p[shoulder].p:shoulderAnchor(head,hip,p[elbow].p,s);
                const Point shoulderCentre=p[Neck].valid?p[Neck].p:add(head,mul(down,scale*.28f));
                segment(shoulderCentre,sleeve,soldier?.10f:.085f,soldier?.10f:.085f,forearm);
                segment(sleeve,p[elbow].p,soldier?.105f:.095f,.055f,forearm);
                if(p[hand].valid && segment(p[elbow].p,p[hand].p,.067f,.032f,hand))
                {
                    const Point direction=mul(sub(p[hand].p,p[elbow].p),1/length(sub(p[hand].p,p[elbow].p)));
                    const Point lateralHand={-direction.y,direction.x};
                    polygon(p[hand].p,lateralHand,direction,
                        {{-.038f,-.025f},{.038f,-.025f},{.052f,.035f},{.037f,.10f},
                         {-.024f,.115f},{-.049f,.055f}},hand);
                }
            }
            if(p[thigh].valid&&p[foot].valid)
            {
                const Point a=p[thigh].p,b=p[foot].p;
                // Thigh samples can sit below the pelvis. Cover that connection
                // instead of leaving the skeleton floating in an empty gap.
                segment(hip,a,.095f,.115f,thigh);
                const float len=length(sub(b,a));
                if(len<scale*.15f||len>scale*3) continue;
                const Point lowerStart=p[knee].valid?p[knee].p:a;
                const float lowerLength=length(sub(b,lowerStart));
                if(lowerLength<1) continue;
                const Point axis=mul(sub(b,lowerStart),1/lowerLength),across={-axis.y,axis.x};
                const Point ankle=b;
                if(p[knee].valid) segment(a,lowerStart,soldier?.155f:.145f,.09f,thigh);
                if(!segment(lowerStart,ankle,p[knee].valid?.12f:.14f,.052f,thigh)) continue;
                const int toe=s?RToe:LToe;
                const Point toeDelta=p[toe].valid?sub(p[toe].p,b):Point{};
                const float toeLength=length(toeDelta);
                // Toe samples orient the shoe during strides. If foreshortened
                // or unavailable, keep a compact shoe around the ankle.
                const bool toeUsable=p[toe].valid&&toeLength>=scale*.025f&&toeLength<=scale*.5f;
                const Point shoeAxis=toeUsable?mul(toeDelta,1/toeLength):axis;
                const Point shoeSide={-shoeAxis.y,shoeAxis.x};
                // Trim the extra toe extension without shrinking past the
                // measured toe. A fuller heel and narrow rounded toe avoid a
                // long, flat paddle while retaining the existing foot anchor.
                const float shoeReach=toeUsable
                    ?(std::max)(toeLength/scale+.018f,(toeLength/scale+.045f)*.85f)
                    :.1275f;
                polygon(b,shoeSide,shoeAxis,{{-.065f,-.05f},{.065f,-.05f},
                    {.075f,shoeReach*.45f},{.060f,shoeReach*.85f},{.023f,shoeReach},
                    {-.023f,shoeReach},{-.065f,shoeReach*.85f},{-.075f,shoeReach*.45f}},foot);
                if(detailed)
                {
                    const Point kneePoint=p[knee].valid?p[knee].p:add(a,mul(sub(ankle,a),.52f));
                    polygon(kneePoint,across,axis,{{-.061f,-.075f},{.061f,-.075f},{.075f,-.025f},
                        {.06f,.075f},{-.06f,.075f},{-.075f,-.025f}},thigh,true);
                }
            }
        }
        const bool fittedTorso=p[LShoulder].valid&&p[RShoulder].valid&&p[Neck].valid;
        if(fittedTorso)
        {
            // A convex hull follows shoulder/hip rotation and lean, rather than
            // forcing a front-facing chest onto a side-facing character.
            // Shoulder/hip span collapses in profile, but the chest still has
            // depth. Keep a modest surface thickness around the measured axes.
            const float shoulderSpan=length(sub(p[LShoulder].p,p[RShoulder].p))/scale;
            const float shoulderPad=scale*(std::max)(
                (soldier?.060f:.048f)+(std::max)(0.f,.26f-shoulderSpan)*.30f,
                .20f-shoulderSpan*.5f);
            const auto addRing=[](Polygon& points,Point centre,float radius) {
                for(int i=0;i<8;++i) {const float a=i*6.2831853f/8;
                    points.push_back(add(centre,{std::cos(a)*radius,std::sin(a)*radius}));}
            };
            const Point shoulderCentre=mul(add(p[LShoulder].p,p[RShoulder].p),.5f);
            // Pelvis is the stable trunk anchor. Animated thigh separation must
            // not enlarge the torso when the legs stride sideways.
            const Point trunk=sub(hip,shoulderCentre);
            const float trunkLength=length(trunk);
            const Point trunkDown=trunkLength>1?mul(trunk,1/trunkLength):down;
            const Point trunkSide={-trunkDown.y,trunkDown.x};
            const auto lateral=[&](Point q){auto d=sub(q,shoulderCentre);return std::fabs(d.x*trunkSide.x+d.y*trunkSide.y);};
            // A side view retains ribcage depth even when the shoulder samples
            // overlap. Apply a minimum surface thickness without moving joints.
            const float chestHalf=(std::max)(scale*.24f,
                (std::max)(lateral(p[LShoulder].p),lateral(p[RShoulder].p))+shoulderPad);
            const float hipsHalf=(std::clamp)(chestHalf*.78f,scale*.115f,scale*.235f);
            const float waistHalf=(std::min)(chestHalf*.68f,hipsHalf*.87f);
            const auto widthAt=[&](float t) {
                const auto blend=[](float a,float b,float t){t=(std::clamp)(t,0.f,1.f);return a+(b-a)*t*t*(3-2*t);};
                if(t<.68f)return blend(chestHalf*.79f,waistHalf,(t-.28f)/.40f);
                if(t<.94f)return blend(waistHalf,hipsHalf,(t-.68f)/.26f);
                // Tuck the lower pelvis inside the thigh volumes instead of
                // ending the torso as a wide, flat hem above close-set legs.
                return blend(hipsHalf,hipsHalf*.70f,(t-.94f)/.14f);
            };
            const auto edgeAt=[&](float t,float sign){return add(add(shoulderCentre,mul(trunk,t)),mul(trunkSide,sign*widthAt(t)));};
            Polygon upper;upper.region=Spine;
            addRing(upper,p[LShoulder].p,shoulderPad);addRing(upper,p[RShoulder].p,shoulderPad);
            upper.push_back(edgeAt(.28f,-1));upper.push_back(edgeAt(.28f,1));
            auto hull=convexHull(std::move(upper));
            if(hull.size()>=3)shapes.push_back(std::move(hull));
            // Smooth width interpolation, split into convex strips for both
            // renderers. A shared edge joins strips without square hip corners.
            constexpr int strips=12;
            for(int i=0;i<strips;++i) {
                const float a=.28f+.80f*static_cast<float>(i)/strips;
                const float b=.28f+.80f*static_cast<float>(i+1)/strips;
                Polygon band{edgeAt(a,-1),edgeAt(a,1),edgeAt(b,1),edgeAt(b,-1)};
                band.region=a<.70f?Spine:Pelvis;shapes.push_back(std::move(band));
            }
            if(detailed) {
                Polygon carrier{edgeAt(.31f,-.70f),edgeAt(.31f,.70f),edgeAt(.62f,.70f),edgeAt(.62f,-.70f)};
                carrier.region=Spine;carrier.detail=true;
                shapes.push_back(rounded(convexHull(std::move(carrier)),2,.12f));
            }
        }
        else if(soldier)
        {
        body({{-.10f,.16f},{.10f,.16f},{.25f,.28f},{.20f,.70f},
            {.17f,.90f},{-.17f,.90f},{-.20f,.70f},{-.25f,.28f}},Spine);
        body({{-.19f,.84f},{.19f,.84f},{.205f,1.01f},{.14f,1.09f},
            {-.14f,1.09f},{-.205f,1.01f}},Pelvis);
        if(detailed)
        {
            body({{-.11f,.28f},{.11f,.28f},{.16f,.36f},{.145f,.62f},
                {-.145f,.62f},{-.16f,.36f}},Spine,true);
            body({{-.205f,.27f},{-.155f,.245f},{-.125f,.39f},{-.175f,.405f}},Spine,true);
            body({{.155f,.245f},{.205f,.27f},{.175f,.405f},{.125f,.39f}},Spine,true);
            for(int i=-1;i<=1;++i)
            {
                const float x=i*.105f;
                body({{x-.043f,.65f},{x+.043f,.65f},{x+.043f,.81f},{x-.043f,.81f}},Spine,true);
            }
            body({{-.175f,.87f},{.175f,.87f},{.18f,.94f},{-.18f,.94f}},Pelvis,true);
            body({{-.045f,.875f},{.045f,.875f},{.045f,.935f},{-.045f,.935f}},Pelvis,true);
        }
        }
        else
        {
            // Ordinary shirt and trousers, without armor panels, pouches or belt kit.
            body({{-.085f,.17f},{.085f,.17f},{.215f,.29f},
                {.155f,1.035f},{-.155f,1.035f},{-.215f,.29f}},Spine);
        }
        if(p[Neck].valid) segment(head,p[Neck].p,.06f,.065f,Neck);
        else body({{-.065f,.075f},{.065f,.075f},{.065f,.21f},{-.065f,.21f}},Neck);
        const bool helmet=headwear==Headwear::Helmet || (headwear==Headwear::Automatic && soldier);
        if(helmet)
        {
            body({{-.105f,-.07f},{.105f,-.07f},{.10f,.07f},{.055f,.145f},
                {-.055f,.145f},{-.10f,.07f}},Head);
            body({{-.07f,-.18f},{.07f,-.18f},{.125f,-.125f},{.14f,-.025f},
                {.12f,.025f},{-.12f,.025f},{-.14f,-.025f},{-.125f,-.125f}},Head);
        }
        else
        {
            // Cranium, cheeks and a narrower jaw instead of an oval bubble.
            body({{-.05f,-.13f},{.05f,-.13f},{.095f,-.075f},{.09f,.045f},
                {.045f,.13f},{-.045f,.13f},{-.09f,.045f},{-.095f,-.075f}},Head);
            if(headwear==Headwear::Cap)
            {
                // Generic cap crown/brim. Actual facing/mesh is not available here.
                body({{-.075f,-.145f},{.065f,-.145f},{.105f,-.09f},{.11f,-.025f},
                    {-.11f,-.025f},{-.11f,-.09f}},Head);
                body({{-.11f,-.035f},{.105f,-.035f},{.205f,.005f},{.19f,.025f},
                    {-.11f,.005f}},Head);
            }
        }
        for(auto& shape:shapes)
        {
            // Fuller head surface to balance the clothed torso and limbs.
            // This does not alter the aim point or compensate for projection error.
            if(shape.region==Head)for(auto& q:shape)q=add(head,mul(sub(q,head),1.36f));
            float area=0;
            for(size_t i=0;i<shape.size();++i)
            {
                const auto a=shape[i],b=shape[(i+1)%shape.size()];
                area+=a.x*b.y-b.x*a.y;
            }
            if(area<0) std::reverse(shape.begin(),shape.end());
        }
        return shapes;
    }
}
