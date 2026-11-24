#include "core/combat.h"

#include <cmath>
#include <initializer_list>

namespace sp {
namespace {

struct DirTable {
    int32_t x[360];
    int32_t y[360];
    DirTable() {
        // Rounded to 1/4096 so libm differences between platforms cannot leak in.
        for (int d = 0; d < 360; ++d) {
            const double r = d * 3.14159265358979323846 / 180.0;
            x[d] = static_cast<int32_t>(std::lround(std::cos(r) * kDirScale));
            y[d] = static_cast<int32_t>(std::lround(std::sin(r) * kDirScale));
        }
    }
};

const DirTable& dirTable() {
    static const DirTable table;
    return table;
}

struct P {
    int64_t x, y;
};

int64_t cross(int64_t ax, int64_t ay, int64_t bx, int64_t by) { return ax * by - ay * bx; }

int64_t isqrt(int64_t v) {
    if (v <= 0) return 0;
    int64_t r = static_cast<int64_t>(std::sqrt(static_cast<double>(v)));
    while (r * r > v) --r;
    while ((r + 1) * (r + 1) <= v) ++r;
    return r;
}

bool pointInBox(const Box& b, int64_t x, int64_t y) {
    return x >= b.x && x <= static_cast<int64_t>(b.x) + b.w && y >= b.y && y <= static_cast<int64_t>(b.y) + b.h;
}

bool angleInArc(const ArcRegion& a, int64_t dx, int64_t dy) {
    if (a.hi - a.lo >= 360) return true;
    int32_t lx, ly, hx, hy;
    dirFromDegrees(a.lo, lx, ly);
    dirFromDegrees(a.hi, hx, hy);
    const int64_t cLo = cross(lx, ly, dx, dy);  // >= 0: d is CCW of lo
    const int64_t cHi = cross(dx, dy, hx, hy);  // >= 0: d is CW of hi
    if (a.hi - a.lo <= 180) return cLo >= 0 && cHi >= 0;
    return cLo >= 0 || cHi >= 0;
}

int orient(P a, P b, P c) {
    const int64_t v = cross(b.x - a.x, b.y - a.y, c.x - a.x, c.y - a.y);
    return (v > 0) - (v < 0);
}

bool onSegment(P a, P b, P p) {
    return p.x >= (a.x < b.x ? a.x : b.x) && p.x <= (a.x > b.x ? a.x : b.x) && p.y >= (a.y < b.y ? a.y : b.y) &&
           p.y <= (a.y > b.y ? a.y : b.y);
}

bool segmentsIntersect(P a, P b, P c, P d) {
    const int o1 = orient(a, b, c), o2 = orient(a, b, d), o3 = orient(c, d, a), o4 = orient(c, d, b);
    if (o1 != o2 && o3 != o4) return true;
    if (o1 == 0 && onSegment(a, b, c)) return true;
    if (o2 == 0 && onSegment(a, b, d)) return true;
    if (o3 == 0 && onSegment(c, d, a)) return true;
    if (o4 == 0 && onSegment(c, d, b)) return true;
    return false;
}

bool segmentHitsBox(P a, P b, const Box& box) {
    if (pointInBox(box, a.x, a.y) || pointInBox(box, b.x, b.y)) return true;
    const P c[4] = {{box.x, box.y}, {static_cast<int64_t>(box.x) + box.w, box.y},
                    {static_cast<int64_t>(box.x) + box.w, static_cast<int64_t>(box.y) + box.h},
                    {box.x, static_cast<int64_t>(box.y) + box.h}};
    for (int i = 0; i < 4; ++i)
        if (segmentsIntersect(a, b, c[i], c[(i + 1) & 3])) return true;
    return false;
}

// Does the circle of radius r (restricted to the arc's angle range) cross any box edge?
bool circleArcCrossesBox(const ArcRegion& a, int64_t r, const Box& box) {
    if (r <= 0) return false;
    const int64_t r2 = r * r;
    const int64_t x0 = box.x, x1 = static_cast<int64_t>(box.x) + box.w;
    const int64_t y0 = box.y, y1 = static_cast<int64_t>(box.y) + box.h;
    for (int64_t x : {x0, x1}) {
        const int64_t dx = x - a.cx;
        const int64_t rem = r2 - dx * dx;
        if (rem < 0) continue;
        const int64_t dy = isqrt(rem);
        for (int64_t sy : {dy, -dy}) {
            const int64_t y = a.cy + sy;
            if (y >= y0 && y <= y1 && angleInArc(a, dx, sy)) return true;
        }
    }
    for (int64_t y : {y0, y1}) {
        const int64_t dy = y - a.cy;
        const int64_t rem = r2 - dy * dy;
        if (rem < 0) continue;
        const int64_t dx = isqrt(rem);
        for (int64_t sx : {dx, -dx}) {
            const int64_t x = a.cx + sx;
            if (x >= x0 && x <= x1 && angleInArc(a, sx, dy)) return true;
        }
    }
    return false;
}

P arcPoint(const ArcRegion& a, int deg, fx r) {
    int32_t dx, dy;
    dirFromDegrees(deg, dx, dy);
    return {a.cx + static_cast<int64_t>(dx) * r / kDirScale, a.cy + static_cast<int64_t>(dy) * r / kDirScale};
}

}  // namespace

void dirFromDegrees(int degrees, int32_t& dx, int32_t& dy) {
    int d = degrees % 360;
    if (d < 0) d += 360;
    dx = dirTable().x[d];
    dy = dirTable().y[d];
}

bool boxesOverlap(const Box& a, const Box& b) {
    return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
}

bool pointInArc(const ArcRegion& a, int64_t px, int64_t py) {
    const int64_t dx = px - a.cx, dy = py - a.cy;
    const int64_t d2 = dx * dx + dy * dy;
    const int64_t r0 = a.r0, r1 = a.r1;
    if (d2 < r0 * r0 || d2 > r1 * r1) return false;
    if (dx == 0 && dy == 0) return true;
    return angleInArc(a, dx, dy);
}

// Two closed regions intersect iff one contains a point of the other or their
// boundaries cross. The arc boundary is two radial segments and two circular arcs.
bool arcOverlapsBox(const ArcRegion& a, const Box& box) {
    const int64_t x0 = box.x, x1 = static_cast<int64_t>(box.x) + box.w;
    const int64_t y0 = box.y, y1 = static_cast<int64_t>(box.y) + box.h;
    if (pointInArc(a, x0, y0) || pointInArc(a, x1, y0) || pointInArc(a, x0, y1) || pointInArc(a, x1, y1)) return true;

    const P loIn = arcPoint(a, a.lo, a.r0), loOut = arcPoint(a, a.lo, a.r1);
    const P hiIn = arcPoint(a, a.hi, a.r0), hiOut = arcPoint(a, a.hi, a.r1);
    if (segmentHitsBox(loIn, loOut, box) || segmentHitsBox(hiIn, hiOut, box)) return true;

    return circleArcCrossesBox(a, a.r1, box) || circleArcCrossesBox(a, a.r0, box);
}

Box worldToLocal(const Box& world, Vec2 origin, int facing) {
    Box b;
    b.y = world.y - origin.y;
    b.w = world.w;
    b.h = world.h;
    b.x = facing > 0 ? world.x - origin.x : origin.x - (world.x + world.w);
    return b;
}

Box localToWorld(const Box& local, Vec2 origin, int facing) {
    Box b;
    b.y = origin.y + local.y;
    b.w = local.w;
    b.h = local.h;
    b.x = facing > 0 ? origin.x + local.x : origin.x - local.x - local.w;
    return b;
}

}  // namespace sp
