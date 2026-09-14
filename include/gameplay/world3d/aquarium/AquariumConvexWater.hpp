#pragma once

#include "gameplay/world3d/aquarium/AquariumBodyNavigation.hpp"
#include <algorithm>
#include <cmath>

namespace pr::gameplay::world3d::aquarium {

// Exact fast path, not a simplified collision mesh. The intersection of inward
// edge half-planes eroded by body radius is convex: valid segment endpoints
// imply the complete swept segment is valid. Layered/concave water falls back.
struct AquariumConvexWater {
    std::vector<std::array<float,3>> planes;
    float bottom=0, top=0;

    static AquariumConvexWater compile(const AquariumNavigation& navigation) {
        AquariumConvexWater out;
        if (!navigation.valid || navigation.layers.size()!=1) return out;
        const auto& layer=navigation.layers.front();
        if (layer.polygons.size()!=1 || layer.polygons.front().size()!=1) return out;
        const auto& ring=layer.polygons.front().front();
        if (ring.size()<3) return out;
        double area=0;
        for (std::size_t i=0;i<ring.size();++i) {
            const auto a=ring[i], b=ring[(i+1)%ring.size()];
            area+=double(a[0])*b[1]-double(b[0])*a[1];
        }
        if (!std::isfinite(area) || std::abs(area)<0.000001) return out;
        const float sign=area>0 ? 1.0f : -1.0f;
        for (std::size_t i=0;i<ring.size();++i) {
            const auto a=ring[i],b=ring[(i+1)%ring.size()];
            const float dx=b[0]-a[0], dz=b[1]-a[1], length=std::hypot(dx,dz);
            if (length<0.000001f) return {};
            const float nx=-dz/length*sign, nz=dx/length*sign, offset=nx*a[0]+nz*a[1];
            for (auto p:ring) if (nx*p[0]+nz*p[1]<offset-0.000001f) return {};
            out.planes.push_back({nx,nz,offset});
        }
        out.bottom=layer.y_bottom; out.top=layer.y_top;
        return out;
    }

    bool contains(const AquariumBodyClearance& body, Point3 p) const {
        for (float v:p) if (!std::isfinite(v)) return false;
        if (p[1]+std::min(0.0f,body.lower_extent)<bottom ||
            p[1]+std::max(0.0f,body.upper_extent)>top) return false;
        for (const auto& plane:planes)
            if (plane[0]*p[0]+plane[1]*p[2]<plane[2]+body.radius) return false;
        return true;
    }
};
} // namespace pr::gameplay::world3d::aquarium
