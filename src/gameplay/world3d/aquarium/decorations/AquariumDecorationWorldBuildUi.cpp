#include "gameplay/world3d/aquarium/decorations/AquariumDecorationUi.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace pr::gameplay::world3d::aquarium::decorations {
namespace {

enum class Icon { Catalog, Save, Cancel, Place, Rotate, Erase, Pan, ZoomIn, ZoomOut };

void drawLine(SDL_Renderer* renderer,float x0,float y0,float x1,float y1,int width=3) {
    for(int offset=-width/2;offset<=width/2;++offset)
        SDL_RenderDrawLineF(renderer,x0+offset,y0,x1+offset,y1);
}

void drawIcon(SDL_Renderer* renderer,const SDL_Rect& rect,Icon icon) {
    const float cx=rect.x+rect.w*.5f,cy=rect.y+rect.h*.5f;
    SDL_SetRenderDrawColor(renderer,248,252,246,255);
    const auto arrow=[&](float dx,float dy,float length=10.0f) {
        drawLine(renderer,cx-dx*length*.25f,cy-dy*length*.25f,cx+dx*length,cy+dy*length,4);
        drawLine(renderer,cx+dx*length,cy+dy*length,cx+dx*3-dy*5,cy+dy*3+dx*5,4);
        drawLine(renderer,cx+dx*length,cy+dy*length,cx+dx*3+dy*5,cy+dy*3-dx*5,4);
    };
    if(icon==Icon::Pan) {
        arrow(0,-1,11);arrow(0,1,11);arrow(-1,0,11);arrow(1,0,11);
    } else if(icon==Icon::ZoomIn||icon==Icon::ZoomOut) {
        const float direction=icon==Icon::ZoomOut?-1.0f:1.0f;
        SDL_Vertex vertices[]={{{cx,cy+direction*13},{248,252,246,255},{}},
            {{cx-10,cy-direction*6},{248,252,246,255},{}},
            {{cx+10,cy-direction*6},{248,252,246,255},{}}};
        SDL_RenderGeometry(renderer,nullptr,vertices,3,nullptr,0);
    } else if(icon==Icon::Save) {
        drawLine(renderer,cx-11,cy,cx-3,cy+9,5);drawLine(renderer,cx-3,cy+9,cx+13,cy-10,5);
    } else if(icon==Icon::Cancel) {
        drawLine(renderer,cx-10,cy-10,cx+10,cy+10,5);drawLine(renderer,cx+10,cy-10,cx-10,cy+10,5);
    } else if(icon==Icon::Catalog) {
        for(int row=-1;row<=1;++row)for(int col=-1;col<=1;++col) {
            SDL_Rect cell{int(cx+col*10-3),int(cy+row*10-3),7,7};SDL_RenderFillRect(renderer,&cell);
        }
    } else if(icon==Icon::Place) {
        SDL_Rect base{int(cx-11),int(cy-8),22,18};SDL_RenderDrawRect(renderer,&base);
        drawLine(renderer,cx,cy-15,cx,cy-2,4);drawLine(renderer,cx-6,cy-9,cx,cy-15,4);
        drawLine(renderer,cx+6,cy-9,cx,cy-15,4);
    } else if(icon==Icon::Erase) {
        SDL_Rect bin{int(cx-9),int(cy-7),18,19};SDL_RenderDrawRect(renderer,&bin);
        drawLine(renderer,cx-12,cy-11,cx+12,cy-11,4);drawLine(renderer,cx-5,cy-15,cx+5,cy-15,3);
    } else if(icon==Icon::Rotate) {
        const float direction=1.0f;
        for(int step=0;step<14;++step) {
            const float a=(-2.35f+step*.32f)*direction,b=(-2.35f+(step+1)*.32f)*direction;
            drawLine(renderer,cx+std::cos(a)*12,cy+std::sin(a)*12,
                cx+std::cos(b)*12,cy+std::sin(b)*12,3);
        }
        arrow(direction,0);
    }
}

} // namespace

WorldBuildHudLayout worldBuildHudLayout(int width,int height) {
    const int w=std::max(1,width),h=std::max(1,height);
    const int margin=std::clamp(std::min(w,h)/36,12,22);
    const int size=std::clamp(h/11,52,68),gap=std::clamp(size/6,8,12);
    WorldBuildHudLayout out;
    out.catalog={margin,margin,size,size};
    out.rotate={margin+size+gap,margin,size,size};
    const int bottom=h-margin-size;
    out.place={margin,bottom,size,size};out.save={w-margin-size,bottom,size,size};
    out.cancel={out.save.x-gap-size,bottom,size,size};out.erase={out.cancel.x-gap-size,bottom,size,size};
    const int camera_size=std::clamp(size*3/4,42,52);
    const int camera_x=w-margin-camera_size,camera_y=h/2-camera_size/2;
    out.camera_pan={camera_x,camera_y,camera_size,camera_size};
    out.zoom_out={camera_x,camera_y-camera_size-6,camera_size,camera_size};
    out.zoom_in={camera_x,camera_y+camera_size+6,camera_size,camera_size};
    return out;
}

void Ui::renderWorldBuildControls(const std::string& root,int w,int h) {
    const auto layout=worldBuildHudLayout(w,h);
    const auto button=[&](const SDL_Rect& rect,const char* id,Icon icon,Color fill) {
        OverlayButton view;view.anchor=OverlayAnchor::TopLeft;view.id=id;
        view.style.width=rect.w;view.style.height=rect.h;view.style.margin_x=rect.x;view.style.margin_y=rect.y;
        view.style.corner_radius=rect.w/2;view.style.fill=fill;view.style.stroke={235,247,239,245};
        view.style.stroke_width=3;canvas_->renderButton(renderer_,root,view);drawIcon(renderer_,rect,icon);
    };
    const Color blue{25,91,130,235},green{47,146,102,240},red{176,66,76,240},gold{180,135,42,240};
    button(layout.catalog,"world_catalog",Icon::Catalog,blue);
    button(layout.save,"world_save",Icon::Save,green);button(layout.cancel,"world_cancel",Icon::Cancel,red);
    button(layout.place,"world_place",Icon::Place,green);button(layout.rotate,"world_rotate",Icon::Rotate,gold);
    button(layout.erase,"world_erase",Icon::Erase,red);
    button(layout.zoom_out,"world_zoom_out",Icon::ZoomOut,blue);
    button(layout.camera_pan,"world_camera_pan",Icon::Pan,{39,151,169,240});
    button(layout.zoom_in,"world_zoom_in",Icon::ZoomIn,blue);
}

const Pixels& Ui::rasterizeWorldBuildHint(const std::string& root,int w,int h,std::string_view status) {
    const std::string key="world-build-controls:"+std::to_string(w)+":"+std::to_string(h)+":"+std::string(status);
    if(pixels_.key==key)return pixels_;
    if(!surface_||pixels_.width!=w||pixels_.height!=h) {
        reset();surface_=SDL_CreateRGBSurfaceWithFormat(0,w,h,32,SDL_PIXELFORMAT_RGBA32);
        if(!surface_)return pixels_;renderer_=SDL_CreateSoftwareRenderer(surface_);
        if(!renderer_)return pixels_;canvas_=std::make_unique<OverlayCanvas>(w,h);pixels_.width=w;pixels_.height=h;
    }
    SDL_SetRenderDrawBlendMode(renderer_,SDL_BLENDMODE_NONE);SDL_SetRenderDrawColor(renderer_,0,0,0,0);SDL_RenderClear(renderer_);
    SDL_SetRenderDrawBlendMode(renderer_,SDL_BLENDMODE_BLEND);renderWorldBuildControls(root,w,h);
    if(!status.empty()) {
        OverlayButton message;message.anchor=OverlayAnchor::TopLeft;message.id="world_build_status";message.label=std::string(status);
        message.style.width=std::min(std::max(1,w-36),680);message.style.height=38;
        message.style.margin_x=18;message.style.margin_y=h-58;message.style.font_size=16;
        message.style.fill={90,35,42,230};message.style.stroke={255,171,137,255};canvas_->renderButton(renderer_,root,message);
    }
    SDL_RenderPresent(renderer_);pixels_.rgba.resize(std::size_t(w)*h*4);
    for(int row=0;row<h;++row)std::memcpy(pixels_.rgba.data()+std::size_t(row)*w*4,
        static_cast<Uint8*>(surface_->pixels)+row*surface_->pitch,std::size_t(w)*4);
    pixels_.key=key;return pixels_;
}

} // namespace pr::gameplay::world3d::aquarium::decorations
