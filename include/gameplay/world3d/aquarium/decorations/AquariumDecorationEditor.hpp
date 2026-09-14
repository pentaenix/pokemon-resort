#pragma once
#include "gameplay/world3d/aquarium/decorations/AquariumDecoration.hpp"
#include "gameplay/world3d/aquarium/construction/AquariumPlayerRuntime.hpp"
#include <optional>

namespace pr::gameplay::world3d::aquarium::decorations {
class Editor {
public:
    bool open(const construction::PlayerTankRuntime&, const AquariumNavigation&,
        std::vector<Decoration> objects, Catalog&);
    void close();
    bool active() const { return active_; }
    const std::string& tankId() const { return tank_.design.id; }
    const auto& objects() const { return objects_; }
    const auto& draft() const { return draft_; }
    const auto& selected() const { return selected_; }
    const auto& tank() const { return tank_; }
    bool moving() const { return moving_; }
    bool dirty() const { return objects_!=initial_; }
    bool canUndo() const { return !draft_&&!undo_.empty(); }
    bool canRedo() const { return !draft_&&!redo_.empty(); }
    bool valid() const;
    bool arrangementValid() const;
    const std::string& message() const { return message_; }
    bool chooseAsset(const std::string&);
    bool selectAt(float local_x, float local_z);
    bool beginMove();
    void moveTo(float local_x,float local_z);
    void nudge(int dx,int dz);
    void adjustHeight(int steps);
    void resetHeight();
    void adjustSize(int steps);
    void rotate(int steps);
    bool confirm();
    bool cancel();
    bool erase();
    bool undo();
    bool redo();
    void selectNext();
    std::vector<AquariumPokemonActor> actors(bool editor_view);
private:
    bool beginEdit();
    bool valid(const Decoration&) const;
    void record(std::vector<Decoration>);
    construction::PlayerTankRuntime tank_; // build unused: retain only placement/design fields.
    AquariumNavigation navigation_;
    Catalog* catalog_=nullptr;
    bool active_=false,moving_=false;
    std::vector<Decoration> objects_,initial_;
    std::vector<std::vector<Decoration>> undo_,redo_;
    std::optional<Decoration> draft_;
    std::optional<std::string> selected_;
    std::string message_;
    unsigned sequence_=0;
};
float substrateWorldY(const construction::PlayerTankRuntime&);
AquariumPokemonActor decorationActor(const Decoration&,const Asset&,
    const construction::PlayerTankRuntime&,bool editor_view);
std::vector<std::string> validateRuntimeDecorations(const construction::AquariumDesignDocument&,
    const construction::PlayerAquariumRuntimeSet&,const std::filesystem::path& root);
} // namespace pr::gameplay::world3d::aquarium::decorations
