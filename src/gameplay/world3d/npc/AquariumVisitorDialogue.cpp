#include "gameplay/world3d/npc/AquariumVisitorDialogue.hpp"
#include "core/config/Json.hpp"
#include <algorithm>
#include <filesystem>
#include <iostream>
namespace pr::gameplay::world3d::npc {
VisitorDialogueCatalog loadVisitorDialogueCatalog(const std::string& root) {
    VisitorDialogueCatalog catalog;
    try {
        const auto json=parseJsonFile((std::filesystem::path(root)/"config/gameplay/world3d/aquarium_visitor_dialogue.json").string());
        for(const auto* key:{"general","tank","species","empty","decorations","rocks","corals","plants","tunnel"})
            if(const auto* lines=json.get(key);lines&&lines->isArray())
                for(const auto& line:lines->asArray())if(line.isString()&&!line.asString().empty())catalog[key].push_back(line.asString());
    } catch(const std::exception& e) {
        std::cerr<<"[AquariumVisitors] event=dialogue_fallback reason="<<e.what()<<'\n';
    }
    if(catalog["general"].empty())catalog["general"]={"I love the aquarium!","It's so peaceful in here."};
    return catalog;
}
std::string chooseVisitorDialogue(const VisitorDialogueCatalog& catalog,const VisitorExhibitContext& context,
    std::deque<std::string>& recent,std::mt19937& rng) {
    std::vector<std::string> contextual,general;
    for(const auto& [tag,lines]:catalog) {
        if(tag=="general")general=lines;
        else if(tag=="species"?!context.pokemon.empty():context.tags.count(tag)>0)
            contextual.insert(contextual.end(),lines.begin(),lines.end());
    }
    auto unused=[&](std::vector<std::string> lines){
        lines.erase(std::remove_if(lines.begin(),lines.end(),[&](const auto& line){
            return std::find(recent.begin(),recent.end(),line)!=recent.end();}),lines.end());return lines;
    };
    auto choices=unused(contextual);
    if(choices.empty())choices=unused(general);
    if(choices.empty()) {
        const auto& eligible=contextual.empty()?general:contextual;
        for(const auto& old:recent)if(std::find(eligible.begin(),eligible.end(),old)!=eligible.end()) {
            choices={old};break; // Reuse the least-recent eligible template, not the last line.
        }
        if(choices.empty())choices=eligible;
    }
    if(choices.empty())return "I love the aquarium!";
    auto line=choices[rng()%choices.size()];
    recent.erase(std::remove(recent.begin(),recent.end(),line),recent.end());
    recent.push_back(line);while(recent.size()>32)recent.pop_front();
    const std::string token="{pokemon}";
    if(!context.pokemon.empty()) {
        const auto& name=context.pokemon[rng()%context.pokemon.size()];
        for(auto p=line.find(token);p!=std::string::npos;p=line.find(token,p+name.size()))line.replace(p,token.size(),name);
    }
    return line;
}
}
