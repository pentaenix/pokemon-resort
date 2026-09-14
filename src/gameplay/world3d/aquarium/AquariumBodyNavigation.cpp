#include "gameplay/world3d/aquarium/AquariumBodyNavigation.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <queue>
#include <utility>

namespace pr::gameplay::world3d::aquarium {
namespace {
float distance(Point3 a, Point3 b) {
    return std::sqrt((a[0]-b[0])*(a[0]-b[0]) + (a[1]-b[1])*(a[1]-b[1]) +
        (a[2]-b[2])*(a[2]-b[2]));
}
constexpr float kSeamEpsilon = 0.0001f;
}

bool containsAquariumBody(
    const AquariumNavigation& nav, const AquariumBodyClearance& body, Point3 p) {
    for (float value : p) if (!std::isfinite(value)) return false;
    if (body.floor) {
        p[1] = body.floor_query_y;
        return containsPoint(nav, p, body.radius);
    }
    const float low = p[1] + std::min(0.0f, body.lower_extent);
    const float high = p[1] + std::max(0.0f, body.upper_extent);
    const auto valid = [&](float y) { return containsPoint(nav, {p[0], y, p[2]}, body.radius); };
    if (!valid(low) || !valid(high) || !valid(p[1])) return false;
    // Top/bottom alone can straddle a dry middle band. Probe both sides of
    // every topology transition crossed by the body, without inventing shelves.
    for (const auto& layer : nav.layers) {
        for (float seam : {layer.y_bottom, layer.y_top}) {
            if (seam > low && seam < high &&
                (!valid(std::max(low, seam-kSeamEpsilon)) ||
                 !valid(std::min(high, seam+kSeamEpsilon)))) return false;
        }
    }
    return true;
}

bool aquariumBodySegmentNavigable(const AquariumNavigation& nav,
    const AquariumBodyClearance& body, Point3 from, Point3 to) {
    const int samples = std::max(1, static_cast<int>(std::ceil(distance(from, to)/0.08f)));
    for (int i = 0; i <= samples; ++i) {
        const float t = static_cast<float>(i)/samples;
        if (!containsAquariumBody(nav, body, {
            from[0]+(to[0]-from[0])*t, from[1]+(to[1]-from[1])*t,
            from[2]+(to[2]-from[2])*t})) return false;
    }
    return true;
}

AquariumBodyNavigation::AquariumBodyNavigation(
    AquariumNavigation nav, AquariumBodyClearance body)
    : navigation_(std::move(nav)), body_(body) {
    minimum_.fill(std::numeric_limits<float>::max());
    maximum_.fill(std::numeric_limits<float>::lowest());
    for (const auto& layer : navigation_.layers) {
        minimum_[1] = std::min(minimum_[1], layer.y_bottom);
        maximum_[1] = std::max(maximum_[1], layer.y_top);
        for (const auto& polygon : layer.polygons) {
            if (polygon.empty()) continue;
            for (auto p : polygon.front()) {
                minimum_[0] = std::min(minimum_[0], p[0]);
                maximum_[0] = std::max(maximum_[0], p[0]);
                minimum_[2] = std::min(minimum_[2], p[1]);
                maximum_[2] = std::max(maximum_[2], p[1]);
            }
        }
    }
    minimum_[1] -= std::min(0.0f, body.lower_extent);
    maximum_[1] -= std::max(0.0f, body.upper_extent);
    if (body.floor) {
        heights_.push_back(body.floor_origin_y);
        minimum_[1] = maximum_[1] = body.floor_origin_y;
    } else if (minimum_[1] <= maximum_[1]) {
        const auto add = [&](float y) {
            if (y >= minimum_[1] && y <= maximum_[1]) heights_.push_back(y);
        };
        for (float y = minimum_[1]; y < maximum_[1]; y += 0.5f) add(y);
        add(maximum_[1]);
        add((minimum_[1]+maximum_[1])*0.5f);
        for (const auto& layer : navigation_.layers) {
            // Include thin usable bands that a regular Y lattice could miss.
            const float low = layer.y_bottom-std::min(0.0f, body.lower_extent);
            const float high = layer.y_top-std::max(0.0f, body.upper_extent);
            if (low <= high) add((low+high)*0.5f);
            add(layer.y_bottom-body.lower_extent+kSeamEpsilon);
            add(layer.y_top-body.upper_extent-kSeamEpsilon);
        }
        std::sort(heights_.begin(), heights_.end());
        heights_.erase(std::unique(heights_.begin(), heights_.end(),
            [](float a, float b) { return std::abs(a-b)<kSeamEpsilon; }), heights_.end());
    }
}

Point3 AquariumBodyNavigation::point(Cell c, const Lattice& lattice) const {
    return {minimum_[0]+c[0]*lattice.spacing, heights_[c[1]],
        minimum_[2]+c[2]*lattice.spacing};
}

bool AquariumBodyNavigation::available(Cell c, Lattice& lattice) {
    if (c[0]<0 || c[2]<0 || c[1]<0 || c[1]>=static_cast<int>(heights_.size())) return false;
    const Point3 p = point(c, lattice);
    if (p[0]>maximum_[0] || p[2]>maximum_[2]) return false;
    const auto found = lattice.occupied.find(c);
    if (found != lattice.occupied.end()) return found->second;
    return lattice.occupied.emplace(c, containsAquariumBody(navigation_, body_, p)).first->second;
}

struct AquariumRouteQuery::Search {
    using Cell=std::array<int,3>;
    struct Visit {
        float cost;
        Cell cell;
        bool operator<(const Visit& other) const {
            return cost==other.cost ? cell>other.cell : cost>other.cost;
        }
    };
    Point3 from{},to{};
    bool fine=false, initialized=false;
    unsigned expanded=0;
    std::priority_queue<Visit> open;
    std::map<Cell,float> costs;
    std::map<Cell,Cell> parents;
};

std::shared_ptr<AquariumRouteQuery> AquariumBodyNavigation::beginRoute(Point3 from,Point3 to) {
    auto query=std::make_shared<AquariumRouteQuery>();
    if (!containsAquariumBody(navigation_,body_,from) ||
        !containsAquariumBody(navigation_,body_,to) || heights_.empty()) {
        query->complete=true;
    } else if (aquariumBodySegmentNavigable(navigation_,body_,from,to)) {
        query->result={AquariumRouteStatus::Direct,{to},0};
        query->complete=true;
    } else {
        query->search=std::make_shared<AquariumRouteQuery::Search>();
        query->search->from=from; query->search->to=to;
    }
    return query;
}

void AquariumBodyNavigation::advanceRoute(AquariumRouteQuery& query,unsigned budget) {
    if (query.complete || !query.search || budget==0) return;
    auto& state=*query.search;
    Lattice& lattice=state.fine ? fine_ : coarse_;
    const Point3 from=state.from,to=state.to;
    if (!state.initialized) {
        const auto yi=std::lower_bound(heights_.begin(),heights_.end(),from[1]);
        const Cell start{static_cast<int>(std::round((from[0]-minimum_[0])/lattice.spacing)),
            std::min(static_cast<int>(heights_.size())-1,static_cast<int>(yi-heights_.begin())),
            static_cast<int>(std::round((from[2]-minimum_[2])/lattice.spacing))};
        for (int x=-1;x<=1;++x) for (int y=-1;y<=1;++y) for (int z=-1;z<=1;++z) {
            const Cell c{start[0]+x,start[1]+y,start[2]+z};
            if (!available(c,lattice) ||
                !aquariumBodySegmentNavigable(navigation_,body_,from,point(c,lattice))) continue;
            const float cost=distance(from,point(c,lattice));
            state.costs[c]=cost; state.parents[c]=c;
            state.open.push({cost+distance(point(c,lattice),to),c});
        }
        state.initialized=true;
    }
    constexpr unsigned kExpansionLimit=12000;
    const Cell offsets[]={{-1,0,0},{1,0,0},{0,-1,0},{0,1,0},{0,0,-1},{0,0,1}};
    unsigned work=0;
    while (!state.open.empty() && work<budget && state.expanded<kExpansionLimit) {
        const auto next=state.open.top(); state.open.pop();
        ++work; // Stale queue entries also consume budget.
        const Cell c=next.cell;
        const Point3 p=point(c,lattice);
        if (next.cost>state.costs.at(c)+distance(p,to)+0.0001f) continue;
        ++state.expanded; ++query.result.expanded_nodes;
        if (distance(p,to)<=lattice.spacing*2.0f &&
            aquariumBodySegmentNavigable(navigation_,body_,p,to)) {
            query.result.status=AquariumRouteStatus::Routed;
            query.result.waypoints.push_back(to);
            Cell back=c;
            for (;;) {
                query.result.waypoints.push_back(point(back,lattice));
                if (state.parents.at(back)==back) break;
                back=state.parents.at(back);
            }
            std::reverse(query.result.waypoints.begin(),query.result.waypoints.end());
            // Leave the validated short edges intact. The runtime performs
            // bounded look-ahead smoothing while following, not an unbounded
            // all-pairs string pull at the end of a simulation step.
            query.complete=true;
            query.search.reset();
            return;
        }
        for (Cell delta:offsets) {
            const Cell n{c[0]+delta[0],c[1]+delta[1],c[2]+delta[2]};
            if (!available(n,lattice)) continue;
            const float cost=state.costs.at(c)+distance(p,point(n,lattice));
            const auto known=state.costs.find(n);
            if (known!=state.costs.end() && known->second<=cost) continue;
            const auto key=std::minmax(c,n);
            const auto edge=lattice.edges.find(key);
            const bool connected=edge!=lattice.edges.end() ? edge->second :
                lattice.edges.emplace(key,aquariumBodySegmentNavigable(
                    navigation_,body_,p,point(n,lattice))).first->second;
            if (!connected) continue;
            state.costs[n]=cost; state.parents[n]=c;
            state.open.push({cost+distance(point(n,lattice),to),n});
        }
    }
    if (state.open.empty()) {
        if (!state.fine) {
            const auto expanded=query.result.expanded_nodes;
            state=AquariumRouteQuery::Search{};
            state.from=from; state.to=to; state.fine=true;
            query.result.expanded_nodes=expanded;
        } else { query.complete=true; query.search.reset(); }
    } else if (state.expanded>=kExpansionLimit) {
        query.result.status=AquariumRouteStatus::SearchLimit;
        query.complete=true; query.search.reset();
    }
}

AquariumBodyRoute AquariumBodyNavigation::route(Point3 from,Point3 to) {
    auto query=beginRoute(from,to);
    while (!query->complete) advanceRoute(*query,128);
    return query->result;
}

} // namespace pr::gameplay::world3d::aquarium
