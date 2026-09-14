#pragma once
#include <deque>
#include <map>
#include <random>
#include <set>
#include <string>
#include <vector>
namespace pr::gameplay::world3d::npc {
struct VisitorExhibitContext {
    std::vector<std::string> pokemon;
    std::set<std::string> tags;
};
using VisitorDialogueCatalog=std::map<std::string,std::vector<std::string>>;
VisitorDialogueCatalog loadVisitorDialogueCatalog(const std::string& project_root);
std::string chooseVisitorDialogue(const VisitorDialogueCatalog&,const VisitorExhibitContext&,
    std::deque<std::string>& recent_templates,std::mt19937&);
inline bool visitorCanConverse(unsigned exchanges) { return exchanges<2; }
inline bool beginVisitorExchange(unsigned& exchanges) {
    if(!visitorCanConverse(exchanges))return false;
    ++exchanges;return true;
}
}
