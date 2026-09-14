#include "gameplay/world3d/npc/AquariumVisitors.hpp"
#include "gameplay/world3d/followers/AquariumFollowerPlanner.hpp"
#include <algorithm>
#include <chrono>
#include <iostream>
#include <stdexcept>
namespace npc=pr::gameplay::world3d::npc;
void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
int main(){try{
    const auto config=npc::loadAquariumVisitorConfig(PR_SOURCE_DIR);
    const auto dialogue=npc::loadVisitorDialogueCatalog(PR_SOURCE_DIR);
    std::size_t line_count=0;for(const auto& group:dialogue)line_count+=group.second.size();
    check(line_count>=80,"visitor dialogue catalogue must provide substantial authored variety");
    unsigned exchanges=0;
    check(npc::beginVisitorExchange(exchanges)&&npc::beginVisitorExchange(exchanges)&&
        !npc::beginVisitorExchange(exchanges)&&exchanges==2,"visitors must allow exactly two exchanges per visit");
    std::mt19937 dialogue_rng(2);std::deque<std::string> history;
    npc::VisitorDialogueCatalog sample{{"general",{"hello","welcome"}},
        {"species",{"That {pokemon} looks happy!","I like {pokemon}."}},
        {"rocks",{"Lovely rocks."}},{"tunnel",{"A tunnel!"}}};
    auto spoken=npc::chooseVisitorDialogue(sample,{{"Lanturn"},{"tank"}},history,dialogue_rng);
    check(spoken.find("Lanturn")!=std::string::npos&&spoken.find("{")==std::string::npos,
        "species dialogue must name a resident of the selected tank");
    auto second=npc::chooseVisitorDialogue(sample,{{"Lanturn"},{"tank"}},history,dialogue_rng);
    check(second!=spoken,"successive exchanges must avoid repeating the same template");
    history.clear();
    check(npc::chooseVisitorDialogue(sample,{{},{"rocks"}},history,dialogue_rng)=="Lovely rocks.",
        "decoration dialogue must require that decoration category");
    history.clear();std::string previous_line;
    for(int i=0;i<10;++i) {
        spoken=npc::chooseVisitorDialogue(sample,{},history,dialogue_rng);
        check((spoken=="hello"||spoken=="welcome")&&spoken!=previous_line,
            "general dialogue must neither invent tank contents nor immediately repeat exhausted pools");
        previous_line=spoken;
    }
    check(config.adult_walk_speed_multiplier==.55,"adult visitors must use the configured leisurely pace");
    npc::AquariumVisitorSession session;session.appearances={"a","b","c"};
    for(const auto* expected:{"a","b","c","a","b","c"})
        check(npc::nextAquariumVisitorAppearance(session)==expected,"visitor appearances must cycle before repeating");
    using F=pr::gameplay::world3d::FacingDirection;
    for(const auto facing:{F::North,F::South,F::East,F::West})
        check(npc::aquariumVisitorViewFacing(facing,0,-1)==facing,"north camera must preserve world sprite directions");
    check(npc::aquariumVisitorViewFacing(F::East,1,0)==F::North&&
        npc::aquariumVisitorViewFacing(F::North,1,0)==F::West&&
        npc::aquariumVisitorViewFacing(F::South,1,0)==F::East&&
        npc::aquariumVisitorViewFacing(F::West,1,0)==F::South,
        "east camera must rotate every walk/idle sprite direction without changing movement");
    check(npc::aquariumVisitorViewFacing(F::South,0,1)==F::North&&
        npc::aquariumVisitorViewFacing(F::West,-1,0)==F::North,
        "visitors facing away from south/west cameras must show their backs");
    pr::gameplay::world3d::CharacterSpriteDefinition sprite;sprite.row_north=7;
    check(npc::aquariumVisitorSpriteRow(sprite,F::East,1,0)==7,
        "follower and visitor presentation must use package-specific directional rows");
    namespace followers=pr::gameplay::world3d::followers;
    followers::AquariumFollowerTrailTarget trail_target;
    trail_target.observe(true,{3,4});
    for(int i=0;i<4;++i)trail_target.observe(false,{99,99});
    check(trail_target.cell&&*trail_target.cell==npc::VisitorCell{3,4},
        "stopping or changing facing must retain the last movement target");
    trail_target.observe(true,{4,4});
    check(*trail_target.cell==npc::VisitorCell{4,4},"only a real player step may advance the follow target");
    const auto snake=followers::aquariumFollowerTrailRoute({{1,1},{2,1},{3,1},{3,2},{3,3}}, {1,1},{3,2});
    check(snake==std::vector<npc::VisitorCell>{{2,1},{3,1},{3,2}},
        "normal following must preserve the player's cornering trail instead of cutting across it");
    std::mt19937 rng(42);std::vector<unsigned char> exhibit_floor(12*10,1);
    const npc::VisitorCell owner{5,5},pet{5,6};exhibit_floor[5*12+5]=0;
    std::vector<npc::VisitorWatchSpot> views{{{2,3},F::North,"seen"},{{8,3},F::East,"new"}};
    auto visit=followers::planAquariumFollowerVisit(12,10,exhibit_floor,pet,owner,views,{"seen"},false,rng);
    check(visit&&visit->target.tank_id=="new"&&!visit->route.empty(),
        "aquarium follower must favor an unvisited exhibit over ordinary idle behavior");
    for(auto p:visit->route)check(!(p==owner)&&exhibit_floor[p.y*12+p.x],"follower must avoid owner/tank reservations");
    exhibit_floor[3*12+8]=0;
    visit=followers::planAquariumFollowerVisit(12,10,exhibit_floor,pet,owner,views,{},false,rng);
    check(visit&&visit->target.tank_id=="seen","follower must not claim an NPC-occupied viewing spot");
    visit=followers::planAquariumFollowerVisit(12,10,exhibit_floor,pet,owner,views,{},true,rng);
    check(visit&&visit->target.tank_id.empty()&&!(visit->target.cell==owner),
        "return visits must stop behind the player, not on them");
    check(visit->target.cell==npc::VisitorCell{5,6},"north-facing owner must be followed from the south");
    check(followers::aquariumFollowerBehind(owner,F::East)==npc::VisitorCell{4,5}&&
        followers::aquariumFollowerBehind(owner,F::South)==npc::VisitorCell{5,4}&&
        followers::aquariumFollowerBehind(owner,F::West)==npc::VisitorCell{6,5},
        "behind target must rotate with the owner's world direction");
    exhibit_floor[6*12+5]=0;
    visit=followers::planAquariumFollowerVisit(12,10,exhibit_floor,{3,6},owner,views,{},true,rng);
    check(!visit,"blocked behind target must not silently turn into side-by-side following");
    exhibit_floor[6*12+5]=1;
    views={{{5,5},F::North,"together"},{{4,5},F::North,"together"},{{6,5},F::North,"other"}};
    visit=followers::planAquariumFollowerVisit(12,10,exhibit_floor,pet,owner,views,{},false,rng,F::North,{},true);
    check(visit&&visit->target.cell==npc::VisitorCell{4,5}&&visit->target.facing==F::North&&visit->target.tank_id=="together",
        "idle companion must stand beside the owner facing the same tank, not another exhibit");
    visit=followers::planAquariumFollowerVisit(12,10,exhibit_floor,pet,owner,views,{},true,rng,F::North,{},true);
    check(visit&&visit->target.cell==pet&&visit->target.tank_id.empty(),
        "movement must take priority over the companion viewing invitation");
    visit=followers::planAquariumFollowerVisit(12,10,exhibit_floor,{3,6},owner,views,{},true,rng,F::East,npc::VisitorCell{5,6});
    check(visit&&visit->target.cell==pet,"turning movement must target the owner's actual previous step");
    std::fill(exhibit_floor.begin(),exhibit_floor.end(),0);exhibit_floor[pet.y*12+pet.x]=1;
    visit=followers::planAquariumFollowerVisit(12,10,exhibit_floor,pet,{1,1},views,{},false,rng);
    check(!visit,"trapped follower must wait, never invent a route or teleport");
    check(config.enabled&&config.watch_min_seconds==5&&config.watch_max_seconds==20,
        "visitor configuration must preserve the requested watching range");
    check(npc::aquariumVisitorCapacity(0,config)==0&&npc::aquariumVisitorCapacity(31,config)==0,
        "infeasible/tiny rooms must not receive forced residents");
    check(npc::aquariumVisitorCapacity(320,config)==10&&npc::aquariumVisitorCapacity(16000,config)==24,
        "capacity must scale with accessible area and remain capped");
    auto disabled=config;disabled.enabled=false;
    check(npc::aquariumVisitorCapacity(1000,disabled)==0,"disabled visitor policy must remain empty");
    constexpr int w=12,h=10;
    std::vector<unsigned char> free(w*h,1);
    // Tank blocks the direct route; one-cell corridor is still navigable.
    for(int y=0;y<8;++y)for(int x=4;x<8;++x)free[y*w+x]=0;
    const npc::VisitorCell start{2,2},goal{10,2};
    auto route=npc::aquariumVisitorRoute(w,h,free,start,goal);
    check(!route.empty()&&route.back()==goal,"visitors must route around tanks instead of using a straight line");
    auto previous=start;bool used_gap=false;
    for(auto p:route){
        check(free[p.y*w+p.x]&&std::abs(p.x-previous.x)+std::abs(p.y-previous.y)==1,
            "visitor route crossed tank collision or used a diagonal step");
        used_gap|=p.y>=8;previous=p;
    }
    check(used_gap,"tank detour was not used");
    check(npc::aquariumVisitorRoute(w,h,free,start,{5,2}).empty(),"watch destinations inside tanks must be rejected");
    check(npc::aquariumVisitorRoute(w,h,free,{-1,0},goal).empty(),"invalid endpoints must be rejected");
    for(int y=8;y<h;++y)for(int x=4;x<8;++x)free[y*w+x]=0;
    check(npc::aquariumVisitorRoute(w,h,free,start,goal).empty(),"disconnected rooms must not receive imaginary routes");
    free[8*w+4]=free[8*w+5]=free[8*w+6]=free[8*w+7]=1;
    check(!npc::aquariumVisitorRoute(w,h,free,start,goal).empty(),"dry single-cell tunnel must connect walking components");
    free[8*w+5]=0;
    check(npc::aquariumVisitorRoute(w,h,free,start,goal).empty(),"stationary actor reservation must block a narrow route");
    std::vector<unsigned char> large(128*128,1);std::vector<double> times;
    for(int i=0;i<24;++i){
        const auto begin=std::chrono::steady_clock::now();
        check(!npc::aquariumVisitorRoute(128,128,large,{i,0},{127-i,127}).empty(),"large room route missing");
        times.push_back(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count());
    }
    std::sort(times.begin(),times.end());
    std::cout<<"aquarium_visitor_tests passed route_128x128_p95_ms="<<times[22]<<'\n';return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
