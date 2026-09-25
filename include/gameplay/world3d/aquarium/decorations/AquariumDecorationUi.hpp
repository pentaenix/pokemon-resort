#pragma once
#include "gameplay/world3d/aquarium/decorations/AquariumDecorationEditor.hpp"
#include "gameplay/world3d/aquarium/rooms/AquariumRoomDecorationCatalog.hpp"
#include "ui/overlay/OverlayCanvas.hpp"
#include <array>
#include <map>

namespace pr::gameplay::world3d::aquarium::decorations {
enum class Tool { None, Move, Height, Size, Rotate };
struct Button { int action; SDL_Rect rect; std::string label; };
struct RoomAssetTrayLayout {
    SDL_Rect panel{};
    std::array<SDL_Rect,4> categories{};
    std::array<SDL_Rect,6> assets{};
    SDL_Rect previous{},next{};
};
RoomAssetTrayLayout roomAssetTrayLayout(int width,int height);
// Shared rectangles for hit testing and the high-resolution overlay canvas.
std::vector<Button> buttons(int width,int height,int page,bool selected,float x,float y);
struct Pixels { std::vector<std::uint8_t> rgba; int width=0,height=0; std::string key; };
class Ui {
public:
    ~Ui();
    const Pixels& rasterize(const std::string& root,int width,int height,const Editor&,
        Catalog&,int page,Tool,float x,float y,bool busy,float floor_x=0,float floor_y=0,Category category=Category::Rocks);
    const Pixels& rasterizeRoomAssets(const std::string& root,int width,int height,
        const rooms::RoomDecorationCatalog&,std::string_view category,int page,int selected,bool busy);
private:
    void reset();
    SDL_Texture* thumbnail(const std::string& id,const std::filesystem::path& path);
    SDL_Surface* surface_=nullptr;
    SDL_Renderer* renderer_=nullptr;
    std::unique_ptr<OverlayCanvas> canvas_;
    std::map<std::string,SDL_Texture*> thumbnails_;
    Pixels pixels_;
};
} // namespace pr::gameplay::world3d::aquarium::decorations
