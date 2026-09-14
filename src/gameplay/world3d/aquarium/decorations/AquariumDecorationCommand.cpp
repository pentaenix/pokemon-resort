#include "gameplay/world3d/aquarium/construction/AquariumConstructionSession.hpp"
namespace pr::gameplay::world3d::aquarium::construction {
std::optional<ConstructionCommitCandidate> AquariumConstructionSession::prepareDecorationChange(
    const std::string& tank_id, std::vector<decorations::Decoration> objects) {
    if (state_!=ConstructionState::Selected || !selectedTank() || selectedTank()->id!=tank_id) return std::nullopt;
    AquariumConstructionCommand command;
    command.kind=AquariumCommandKind::EditDecorations; command.tank_id=tank_id;
    for (const auto& tank:committed_.tank_decorations)
        if(tank.tank_id==tank_id) command.decorations_before=tank.objects;
    command.decorations_after=std::move(objects);
    if(command.decorations_before==command.decorations_after) return std::nullopt;
    return prepareHistoryCommand(command,AquariumCommandDirection::Forward,ConstructionHistoryAction::RecordNew);
}
}
