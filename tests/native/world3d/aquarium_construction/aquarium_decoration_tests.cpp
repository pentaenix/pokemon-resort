#include "gameplay/world3d/aquarium/decorations/AquariumDecorationUi.hpp"
#include "gameplay/world3d/aquarium/decorations/AquariumDecorationInteraction.hpp"
#include "gameplay/world3d/aquarium/rendering/AquariumPokemonRenderPose.hpp"
#include "gameplay/attend/rendering/AttendPokemonMaterialPolicy.hpp"
#include "core/input/InputRouter.hpp"
#include "gameplay/world3d/aquarium/construction/AquariumConstructionCommand.hpp"
#include "gameplay/world3d/camera/Gen4FollowCamera.hpp"
#include <SDL_image.h>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace aq=pr::gameplay::world3d::aquarium;
namespace decor=aq::decorations;
namespace construction=aq::construction;
namespace geo=pr::aquarium::geometry;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
int main(){try{
    require(!decor::decorationDragStarted({100,700},{104,703}),"tray click jitter must not become a drag");
    require(decor::decorationDragStarted({100,700},{100,680}),"tray drag must cross click threshold");
    struct PointerProbe:pr::ScreenInput {
        int presses=0,moves=0,releases=0;
        bool handleUnroutedSdlEvent(const SDL_Event& e) override{return !decor::usesDecorationPointerCallback(e);}
        bool handlePointerPressed(int,int) override{++presses;return true;}
        void handlePointerMoved(int,int) override{++moves;}
        bool handlePointerReleased(int,int) override{++releases;return true;}
    } probe;
    pr::InputRouter router;pr::InputConfig input;input.accept_mouse=true;
    SDL_Event event{};event.type=SDL_MOUSEBUTTONDOWN;event.button.button=SDL_BUTTON_LEFT;
    router.handleEvent(event,input,&probe);event.type=SDL_MOUSEMOTION;router.handleEvent(event,input,&probe);
    event.type=SDL_MOUSEBUTTONUP;event.button.button=SDL_BUTTON_LEFT;router.handleEvent(event,input,&probe);
    require(probe.presses==1&&probe.moves==1&&probe.releases==1,
        "decoration raw hook swallowed InputRouter press/move/release callbacks");
    pr::gameplay::world3d::camera::Gen4FollowCamera before({});before.setManualPose({-200,150,400},180,-35);
    auto centered=decor::framedDecorationCamera(before,{300,-24,600},{80,48,120},1280,800,1);
    float sx=0,sy=0,sd=0;
    require(centered.worldToScreen({300,-24,600},1280,800,sx,sy,sd)&&std::abs(sx-640)<.1f&&std::abs(sy-270)<.1f,
        "decoration camera must center selected tank in the area above the tray");
    require(std::abs(centered.pose().forward.y-before.pose().forward.y)<.0001f,"decoration framing changed editor angle");
    const std::filesystem::path root=PR_SOURCE_DIR;
    {
        namespace rendering=aq::rendering;
        namespace attend=pr::gameplay::attend::rendering;
        aq::AquariumPokemonActor prop;prop.id="decoration:test:rock";
        aq::AquariumPokemonActor fish;fish.id="resident:test";
        require(rendering::aquariumUsesClockwiseBackfaceCull(prop)&&!rendering::aquariumUsesClockwiseBackfaceCull(fish),
            "decoration winding correction leaked into curated Pokemon presentation");
        const auto black=attend::loadAttendPokemonModelShared((root/"assets/aquarium/d6rock01_preview.glb").string());
        const auto marine=attend::loadAttendPokemonModelShared((root/"assets/aquarium/Boulder_preview_3DS_simplified.glb").string());
        require(black&&black->valid&&marine&&marine->valid,"decoration winding fixtures failed to load");
        require(std::any_of(black->materials.begin(),black->materials.end(),attend::shouldCullAttendPokemonMaterial),
            "Black rock no longer exercises single-sided rendering");
        require(std::none_of(marine->materials.begin(),marine->materials.end(),attend::shouldCullAttendPokemonMaterial),
            "Marine Park double-sided materials must continue to bypass culling");
        // A glTF front-facing +Z triangle, viewed from +Z, projects CCW in
        // overworld screen coordinates. Culling CCW would remove its exterior.
        pr::gameplay::world3d::camera::Gen4FollowCamera camera({});
        camera.setNearClip(.1f);camera.setManualPose({0,0,10},180,0);
        float x[3],y[3],depth;
        const pr::gameplay::world3d::camera::Vec3 vertices[]={{-1,-1,0},{1,-1,0},{0,1,0}};
        for(int i=0;i<3;++i)require(camera.worldToScreen(vertices[i],800,600,x[i],y[i],depth),"front-face fixture behind camera");
        require((x[1]-x[0])*(y[2]-y[0])-(y[1]-y[0])*(x[2]-x[0])<0,
            "overworld projected winding changed: re-evaluate decoration culling");
    }
    decor::Catalog catalog;catalog.scan(root);
    require(decor::decorationCategory("Boulder.glb")==decor::Category::Rocks&&
        decor::decorationCategory("Coral.glb")==decor::Category::Corals&&
        decor::decorationCategory("Brown Algae.glb")==decor::Category::Plants&&
        decor::decorationCategory("ruins.glb")==decor::Category::Other,"decoration category classification changed");
    std::size_t categorized=0;for(auto c:{decor::Category::Rocks,decor::Category::Corals,decor::Category::Plants,decor::Category::Other})
        categorized+=catalog.indices(c).size();
    require(categorized==catalog.entries().size(),"category filtering loses/duplicates assets");
    require(!catalog.entries().empty(),"decoration GLB catalogue is empty");
    for(const auto& asset:catalog.entries())require(asset.path.parent_path()==root/"assets/aquarium",
        "signs/subdirectory assets leaked into tank decoration catalogue");
    require(!decor::validAssetId("../signs/sign.glb")&&!decor::validAssetId("signs/a.glb"),"unsafe asset paths accepted");
    const auto* asset=catalog.resolve("Rock 1x 1 1_preview_3DS_simplified.glb");
    require(asset&&asset->bounds.valid,"real rock GLB could not be measured");
    construction::PlayerTankRuntime tank;tank.design.id="tank_decor";
    tank.design.footprint.width_cells=tank.design.footprint.depth_cells=12;
    tank.design.depth_steps=4;tank.design.height_steps=12;
    tank.world_center_x=240;tank.world_center_z=160;
    aq::AquariumNavigation nav;nav.valid=true;nav.export_units_per_meter=16;
    nav.layers.push_back({"water",-2+geo::kFlatSandSurfaceWorldUnits/16,5.8f,
        {{{{-5.9f,-5.9f},{5.9f,-5.9f},{5.9f,5.9f},{-5.9f,5.9f}}}}});
    decor::Editor editor;require(editor.open(tank,nav,{},catalog),"decoration editor did not open");
    require(editor.chooseAsset(asset->id)&&editor.valid(),"floor placement was not valid");
    require(editor.draft()->height_steps==0,"default decoration did not start on substrate");
    editor.adjustHeight(8);editor.moveTo(16,24);
    require(editor.draft()->height_steps==8,"lateral movement changed decoration height");
    require(editor.confirm(),"raised rock could not be placed");
    const auto id=editor.objects()[0].id;
    require(editor.beginMove(),"placed rock could not be picked up");editor.moveTo(-16,0);
    require(editor.draft()->height_steps==8&&editor.confirm(),"repositioned rock snapped to floor");
    require(editor.objects()[0].scale_steps==6,"new decorations must start two size ticks larger");
    editor.adjustSize(2);require(editor.confirm()&&editor.objects()[0].scale_steps==8,"placed rock resize failed");
    require(editor.undo()&&editor.objects()[0].scale_steps==6&&editor.redo(),"decoration resize undo/redo failed");
    editor.selectNext();require(editor.beginMove(),"could not reselect rock after history");editor.moveTo(10000,10000);
    require(!editor.valid()&&!editor.confirm()&&editor.cancel(),"outside-glass object was committed");
    require(editor.objects()[0].id==id,"edit/cancel replaced stable decoration ID");
    editor.adjustHeight(-10);require(editor.confirm(),"partial burial was rejected");
    while(editor.objects().size()<decor::kTankDecorationLimit){
        require(editor.chooseAsset(asset->id)&&editor.confirm(),"overlapping floor placement was rejected");
    }
    require(!editor.chooseAsset(asset->id)&&editor.objects().size()==25,"26th decoration was accepted");
    const auto actors=editor.actors(false);
    require(actors.size()==25,"placed decorations failed to produce render instances");
    const auto& floor_actor=actors.back();
    require(std::abs(floor_actor.world_position[1]+asset->bounds.min_y*floor_actor.model_scale-
        decor::substrateWorldY(tank))<.001f,"rock base does not align with kernel sand surface");
    construction::AquariumDesignDocument doc;doc.design_id="decor-document";doc.map_id="aquarium_builder_lab";
    doc.tanks.push_back(tank.design);doc.tank_decorations={{tank.design.id,editor.objects()}};
    const auto json=construction::serializeAquariumDesignCanonical(doc);
    const auto parsed=construction::parseAquariumDesign(json);
    require(parsed.document&&parsed.document->tank_decorations[0].objects==editor.objects(),"decoration save round trip changed transforms");
    require(construction::serializeAquariumDesignCanonical(*parsed.document)==json,"decoration save is not canonical");
    auto excessive=doc;excessive.tank_decorations[0].objects.push_back(editor.objects()[0]);
    require(!construction::validateAquariumDesign(excessive).empty(),"over-limit save was not rejected");
    construction::AquariumConstructionCommand command;command.kind=construction::AquariumCommandKind::EditDecorations;
    command.tank_id=tank.design.id;command.decorations_before=editor.objects();
    auto cleared=construction::applyAquariumConstructionCommand(doc,command,construction::AquariumCommandDirection::Forward);
    require(cleared&&cleared->tank_decorations[0].objects.empty(),"decoration command did not clear objects");
    auto restored=construction::applyAquariumConstructionCommand(*cleared,command,construction::AquariumCommandDirection::Reverse);
    require(restored&&restored->tank_decorations[0].objects==editor.objects(),"decoration command was not reversible");
    require(!construction::applyAquariumConstructionCommand(*cleared,command,construction::AquariumCommandDirection::Forward),"stale decoration command accepted");
    auto hole=nav;hole.layers[0].polygons[0].push_back({{-1,-1},{-1,1},{1,1},{1,-1}});
    decor::Editor blocked;blocked.open(tank,hole,{},catalog);blocked.chooseAsset(asset->id);
    require(!blocked.valid(),"decoration was allowed inside dry tunnel volume");
    pr::gameplay::world3d::camera::Gen4FollowCamera camera({});camera.setNearClip(.1f);
    camera.setManualPose({0,100,0},180,-90);float x=0,y=0,depth=0;
    require(camera.worldToScreen({10,0,0},800,600,x,y,depth)&&x>400&&std::abs(y-300)<.01f,
        "exact top-down camera has singular/flipped screen axes");
    SDL_SetHint(SDL_HINT_VIDEODRIVER,"dummy");require(SDL_Init(SDL_INIT_VIDEO)==0,"SDL preview fixture failed");TTF_Init();
    {
        decor::Ui ui;const auto& pixels=ui.rasterize(root.string(),1280,800,editor,catalog,0,decor::Tool::None,640,320,false,0,0,decor::Category::Plants);
        require(pixels.rgba.size()==1280*800*4,"decoration overlay dimensions do not match logical canvas");
        const auto controls=decor::buttons(1280,800,0,true,640,320);
        for(const auto& button:controls)require(button.label.empty(),"decoration controls must be icon/preview only");
        require(controls[0].rect.y>600 && controls[2].rect.y<80,
            "decoration accept/history must use the construction HUD positions");
        const auto finish_pixel=((controls[0].rect.y+20)*1280+controls[0].rect.x+20)*4;
        require(pixels.rgba[finish_pixel+3]==0,
            "decoration tray covers the shared accept icon");
        for(std::size_t i=0;i<controls.size();++i)for(std::size_t j=i+1;j<controls.size();++j)
            require(!SDL_HasIntersection(&controls[i].rect,&controls[j].rect),
                "decoration controls have overlapping hit targets");
        int plant_pixels=0;
        for(int py=660;py<740;++py)for(int px=72;px<190;++px){
            const auto p=(py*1280+px)*4;
            if(pixels.rgba[p]<50 && pixels.rgba[p+1]<100 && pixels.rgba[p+2]<120)++plant_pixels;
        }
        require(plant_pixels>30,"plant thumbnail vanished: preserve exported negative/repeated UV coordinates");
        SDL_Surface* surface=SDL_CreateRGBSurfaceWithFormatFrom(const_cast<std::uint8_t*>(pixels.rgba.data()),1280,800,32,1280*4,SDL_PIXELFORMAT_RGBA32);
        IMG_SavePNG(surface,"/tmp/aquarium-decoration-ui.png");SDL_FreeSurface(surface);
    }
    TTF_Quit();SDL_Quit();
    std::cout<<"aquarium_decoration_tests: passed (assets="<<catalog.entries().size()<<", limit=25)\n";
    return 0;
}catch(const std::exception& error){std::cerr<<"aquarium_decoration_tests: "<<error.what()<<'\n';return 1;}}
