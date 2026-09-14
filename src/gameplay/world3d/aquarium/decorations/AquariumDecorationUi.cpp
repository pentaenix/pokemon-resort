#include "gameplay/world3d/aquarium/decorations/AquariumDecorationUi.hpp"
#include "gameplay/attend/rendering/AttendPokemonModel.hpp"
#include "gameplay/attend/rendering/AttendPokemonMaterialPolicy.hpp"
#include "gameplay/world3d/aquarium/construction/AquariumConstructionVisual.hpp"
#include <SDL_image.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <sstream>

namespace pr::gameplay::world3d::aquarium::decorations {
std::vector<Button> buttons(int w,int h,int page,bool selected,float x,float y) {
    const auto hud=construction::aquariumDecorationHudLayout(w,h);
    auto rect=[](const auto& r){return SDL_Rect{r.x,r.y,r.width,r.height};};
    std::vector<Button> out{{1,rect(hud.build),{}},{2,rect(hud.cancel),{}},
        {3,rect(hud.undo),{}},{4,rect(hud.redo),{}},
        {14,rect(hud.remove),{}},
        {5,{16,h-144,40,72},{}},{6,{w-330,h-144,40,72},{}}};
    const int slot=std::max(24,(w-410)/6);
    for(int i=0;i<6;++i)out.push_back({100+page*6+i,{64+i*slot,h-156,slot-6,108},{}});
    for(int i=0;i<4;++i)out.push_back({20+i,{64+i*64,h-230,52,52},{}});
    if(selected) {
        const int cx=std::clamp(int(x),130,std::max(130,w-130));
        const int cy=std::clamp(int(y),140,std::max(140,h-290));
        out.push_back({10,{cx-23,cy-23,46,46},{}});
        out.push_back({11,{cx-23,cy-91,46,46},{}});
        out.push_back({12,{cx+45,cy-23,46,46},{}});
        out.push_back({13,{cx-91,cy-23,46,46},{}});
    }
    return out;
}
Ui::~Ui(){reset();}
void Ui::reset() {
    canvas_.reset();
    for(auto [id,texture]:thumbnails_)SDL_DestroyTexture(texture);
    thumbnails_.clear();
    if(renderer_)SDL_DestroyRenderer(renderer_);
    if(surface_)SDL_FreeSurface(surface_);
    renderer_=nullptr;surface_=nullptr;pixels_={};
}
SDL_Texture* Ui::thumbnail(const Asset& asset) {
    auto found=thumbnails_.find(asset.id);if(found!=thumbnails_.end())return found->second;
    auto model=gameplay::attend::rendering::loadAttendPokemonModelShared(asset.path.string());
    if(!model || !model->valid)return nullptr;
    SDL_Surface* image=SDL_CreateRGBSurfaceWithFormat(0,144,90,32,SDL_PIXELFORMAT_RGBA32);
    SDL_Renderer* renderer=SDL_CreateSoftwareRenderer(image);
    if(!renderer){if(image)SDL_FreeSurface(image);return nullptr;}
    SDL_SetRenderDrawColor(renderer,0,0,0,0);SDL_RenderClear(renderer);
    struct Triangle {SDL_Vertex vertices[3];float depth;int material;};
    std::vector<Triangle> triangles;
    auto primitives=model->primitives;
    const auto globals=gameplay::attend::rendering::buildAttendPokemonGlobals(*model,nullptr,0.0);
    const auto skins=gameplay::attend::rendering::buildAttendPokemonSkinMatrices(*model,globals);
    for(auto& primitive:primitives){
        std::vector<gameplay::attend::rendering::AttendPokemonVertex> posed;
        gameplay::attend::rendering::skinAttendPokemonPrimitiveWithPose(*model,primitive,globals,skins,posed);
        primitive.vertices=std::move(posed);
        primitive.default_visible=gameplay::attend::rendering::shouldRenderAttendPokemonPrimitive(*model,primitive);
    }
    std::vector<SDL_Texture*> textures(model->materials.size(),nullptr);
    for(std::size_t i=0;i<model->materials.size();++i){
        const auto& bytes=model->materials[i].base_color_bytes;if(bytes.empty())continue;
        auto* decoded=IMG_Load_RW(SDL_RWFromConstMem(bytes.data(),int(bytes.size())),1);
        if(decoded){textures[i]=SDL_CreateTextureFromSurface(renderer,decoded);SDL_FreeSurface(decoded);}
        if(textures[i])SDL_SetTextureBlendMode(textures[i],SDL_BLENDMODE_BLEND);
    }
    float lo_x=1e30f,hi_x=-1e30f,lo_y=1e30f,hi_y=-1e30f;
    for(const auto& p:primitives)if(p.default_visible)for(const auto& v:p.vertices){
        const float x=(v.x-v.z)*.7071f,y=v.y*.82f-(v.x+v.z)*.405f;
        lo_x=std::min(lo_x,x);hi_x=std::max(hi_x,x);lo_y=std::min(lo_y,y);hi_y=std::max(hi_y,y);
    }
    const float scale=std::min(128/std::max(.001f,hi_x-lo_x),78/std::max(.001f,hi_y-lo_y));
    for(const auto& p:primitives)if(p.default_visible)for(std::size_t i=0;i+2<p.indices.size();i+=3){
        if(p.indices[i]>=p.vertices.size()||p.indices[i+1]>=p.vertices.size()||p.indices[i+2]>=p.vertices.size())continue;
        Triangle t{};t.material=p.material;
        // Exported plant cards use repeated negative V coordinates. Preserve
        // their tile instead of clamping every vertex onto a transparent edge.
        float min_u=1e30f,min_v=1e30f;
        for(int j=0;j<3;++j){const auto& v=p.vertices[p.indices[i+j]];min_u=std::min(min_u,v.u);min_v=std::min(min_v,v.v);}
        const float shift_u=std::floor(min_u),shift_v=std::floor(min_v);
        for(int j=0;j<3;++j){
            const auto& v=p.vertices[p.indices[i+j]];
            const float shade=std::clamp(.6f+.35f*v.ny+.1f*v.nx,.25f,1.0f);
            const float* base=p.material>=0 && std::size_t(p.material)<model->materials.size()
                ? model->materials[p.material].base_color : nullptr;
            auto byte=[&](float color){return Uint8(std::clamp(color*shade*255,0.0f,255.0f));};
            t.vertices[j]={{72+((v.x-v.z)*.7071f-(lo_x+hi_x)*.5f)*scale,
                45-(v.y*.82f-(v.x+v.z)*.405f-(lo_y+hi_y)*.5f)*scale},
                {byte(v.r*(base?base[0]:.6f)),byte(v.g*(base?base[1]:.8f)),byte(v.b*(base?base[2]:.85f)),255},
                {std::clamp(v.u-shift_u,0.0f,1.0f),std::clamp(v.v-shift_v,0.0f,1.0f)}};
            t.depth+=(v.x+v.z)*.58f+v.y*.57f;
        }
        triangles.push_back(t);
    }
    std::stable_sort(triangles.begin(),triangles.end(),[](const auto& a,const auto& b){return a.depth<b.depth;});
    for(const auto& triangle:triangles)SDL_RenderGeometry(renderer,
        triangle.material>=0&&std::size_t(triangle.material)<textures.size()?textures[triangle.material]:nullptr,
        triangle.vertices,3,nullptr,0);
    SDL_RenderPresent(renderer);
    auto* texture=SDL_CreateTextureFromSurface(renderer_,image);
    if(texture)SDL_SetTextureBlendMode(texture,SDL_BLENDMODE_BLEND);
    for(auto* material:textures)if(material)SDL_DestroyTexture(material);
    SDL_DestroyRenderer(renderer);SDL_FreeSurface(image);thumbnails_[asset.id]=texture;return texture;
}
const Pixels& Ui::rasterize(const std::string& root,int w,int h,const Editor& editor,
    Catalog& catalog,int page,Tool tool,float x,float y,bool busy,float floor_x,float floor_y,Category category) {
    const auto entries=catalog.indices(category);
    std::ostringstream key;
    key<<w<<':'<<h<<':'<<page<<':'<<int(category)<<':'<<int(tool)<<':'<<int(x)<<':'<<int(y)<<':'<<busy<<':'
        <<editor.selected().value_or("")<<':'<<editor.message()<<':'<<editor.valid()<<':'<<int(floor_x)<<':'<<int(floor_y);
    key<<serializeJsonValue(serializeDecorations({{editor.tankId(),editor.objects()}}));
    if(editor.draft())key<<serializeJsonValue(serializeDecorations({{editor.tankId(),{*editor.draft()}}}));
    if(pixels_.key==key.str())return pixels_;
    if(!surface_ || pixels_.width!=w || pixels_.height!=h){
        reset();surface_=SDL_CreateRGBSurfaceWithFormat(0,w,h,32,SDL_PIXELFORMAT_RGBA32);
        if(!surface_)return pixels_;
        renderer_=SDL_CreateSoftwareRenderer(surface_);if(!renderer_)return pixels_;
        canvas_=std::make_unique<OverlayCanvas>(w,h);pixels_.width=w;pixels_.height=h;
    }
    SDL_SetRenderDrawBlendMode(renderer_,SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(renderer_,0,0,0,0);SDL_RenderClear(renderer_);
    SDL_SetRenderDrawBlendMode(renderer_,SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer_,18,51,72,240);
    SDL_Rect bar{0,h-176,w-280,176};SDL_RenderFillRect(renderer_,&bar);
    for(auto button:buttons(w,h,page,editor.selected().has_value()||editor.draft().has_value(),x,y)){
        // Standard construction controls are drawn by the existing bgfx HUD.
        if(button.action<=4 || button.action==14)continue;
        OverlayButton view;view.anchor=OverlayAnchor::TopLeft;view.id=std::to_string(button.action);view.label=button.label;
        view.style.width=button.rect.w;view.style.height=button.rect.h;
        view.style.margin_x=button.rect.x;view.style.margin_y=button.rect.y;
        view.style.font_size=18;view.style.corner_radius=button.action>=10&&button.action<14 ? 23 : 16;
        if(button.action>=20&&button.action<=23){
            view.style.corner_radius=26;
            if(button.action-20==int(category))view.style.stroke={255,213,87,255};
        }
        view.style.padding_x=10;
        view.style.fill=button.action==1 ? Color{54,157,122,255} : Color{36,119,160,245};
        if(button.action>=10&&button.action<=13&&int(tool)==button.action-9)view.style.stroke={255,213,87,255};
        if(button.action==2||button.action==14)view.style.fill={183,79,91,245};
        if(button.action>=100) {
            const std::size_t index=button.action-100;
            if(index>=entries.size())continue;
            const auto& asset=catalog.entries()[entries[index]];
            view.label="";canvas_->renderButton(renderer_,root,view);
            if(auto* texture=thumbnail(asset)){
                const float scale=std::min((button.rect.w-8)/144.0f,(button.rect.h-8)/90.0f);
                SDL_Rect preview{button.rect.x+(button.rect.w-int(144*scale))/2,
                    button.rect.y+(button.rect.h-int(90*scale))/2,int(144*scale),int(90*scale)};
                SDL_RenderCopy(renderer_,texture,nullptr,&preview);
            }
        } else {
            canvas_->renderButton(renderer_,root,view);
            const float cx=button.rect.x+button.rect.w*.5f,cy=button.rect.y+button.rect.h*.5f;
            SDL_SetRenderDrawColor(renderer_,255,255,246,255);
            auto line=[&](float ax,float ay,float bx,float by){
                for(int k=-1;k<=1;++k)SDL_RenderDrawLineF(renderer_,cx+ax+k,cy+ay,cx+bx+k,cy+by);
            };
            auto arrow=[&](float dx,float dy){
                line(-dx*9,-dy*9,dx*9,dy*9);
                line(dx*9,dy*9,dx*3-dy*5,dy*3+dx*5);
                line(dx*9,dy*9,dx*3+dy*5,dy*3-dx*5);
            };
            auto triangle=[&](float ax,float ay,float bx,float by,float dx,float dy,SDL_Color color){
                SDL_Vertex vertices[]={{{cx+ax,cy+ay},color,{}},{{cx+bx,cy+by},color,{}},{{cx+dx,cy+dy},color,{}}};
                SDL_RenderGeometry(renderer_,nullptr,vertices,3,nullptr,0);
            };
            if(button.action==5||button.action==6)arrow(button.action==5?-1:1,0);
            if(button.action==10){arrow(1,0);arrow(-1,0);arrow(0,1);arrow(0,-1);}
            if(button.action==11){arrow(0,-1);arrow(0,1);}
            if(button.action==12){arrow(.8f,-.8f);arrow(-.8f,.8f);}
            if(button.action==13){
                for(int i=0;i<20;++i){const float a=i*.24f;line(std::cos(a)*10,std::sin(a)*10,
                    std::cos(a+.24f)*10,std::sin(a+.24f)*10);}
                line(0,-10,-7,-5);line(0,-10,-7,-14);
            }
            if(button.action==20){ // Faceted rock.
                triangle(-15,10,-11,-6,0,-13,{236,235,222,255});
                triangle(-15,10,0,-13,3,10,{205,219,225,255});
                triangle(0,-13,12,-5,3,10,{246,245,231,255});
                triangle(12,-5,15,10,3,10,{157,187,204,255});
            }
            if(button.action==21){ // Branching coral.
                SDL_SetRenderDrawColor(renderer_,255,183,169,255);
                for(int k=-2;k<=2;++k)SDL_RenderDrawLineF(renderer_,cx+k,cy+14,cx+k,cy-13);
                line(0,13,0,-13);
                for(float s:{-1.0f,1.0f}){line(0,5,s*11,-2);line(s*11,-2,s*11,-12);line(s*11,-5,s*16,-9);}
            }
            if(button.action==22){ // Stem and leaves.
                SDL_SetRenderDrawColor(renderer_,191,238,163,255);line(0,14,0,-10);
                triangle(0,3,-14,-4,-12,-14,{185,234,144,255});
                triangle(0,3,-12,-14,-3,-9,{224,246,172,255});
                triangle(0,8,14,0,13,-11,{124,213,151,255});
                triangle(0,8,13,-11,3,-5,{185,234,144,255});
            }
            if(button.action==23){ // Other: simple four-point sparkle.
                for(float s:{-1.0f,1.0f}){
                    triangle(0,-15,s*5,0,0,15,{255,232,151,255});
                    triangle(-15,0,0,s*5,15,0,{255,242,185,255});
                }
            }
        }
    }
    OverlayButton hint;hint.anchor=OverlayAnchor::TopLeft;hint.label=busy?"Saving...":editor.message();
    hint.style.margin_x=w/2-285;hint.style.margin_y=h-274;hint.style.width=570;hint.style.height=34;
    hint.style.font_size=18;hint.style.stroke_width=0;
    if(!hint.label.empty())canvas_->renderButton(renderer_,root,hint);
    SDL_RenderPresent(renderer_);
    pixels_.rgba.resize(std::size_t(w)*h*4);
    for(int row=0;row<h;++row)std::memcpy(pixels_.rgba.data()+std::size_t(row)*w*4,
        static_cast<Uint8*>(surface_->pixels)+row*surface_->pitch,std::size_t(w)*4);
    pixels_.key=key.str();return pixels_;
}
} // namespace pr::gameplay::world3d::aquarium::decorations
