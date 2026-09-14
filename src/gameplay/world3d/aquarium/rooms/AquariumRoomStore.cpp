#include "gameplay/world3d/aquarium/rooms/AquariumRoomStore.hpp"
#include <fstream>
#include <sstream>
#include <fcntl.h>
#include <unistd.h>

namespace pr::gameplay::world3d::aquarium::rooms {
namespace fs=std::filesystem;
namespace {
LayoutLoadResult read(const fs::path& path) {
    std::ifstream input(path,std::ios::binary);
    if (!input) return {LayoutLoadStatus::Invalid,std::nullopt,"Room save unavailable"};
    std::ostringstream text; text<<input.rdbuf();
    return parseBuildingLayout(text.str());
}
void sync(const fs::path& path) {
    const int fd=::open(path.c_str(),O_RDONLY);
    if(fd<0) throw std::runtime_error("Cannot sync room save");
    const int result=::fsync(fd); ::close(fd);
    if(result!=0) throw std::runtime_error("Room save sync failed");
}
}
bool AquariumRoomStore::exists() const {
    return fs::exists(path_) || fs::exists(path_.string()+".bak") || fs::exists(path_.string()+".tmp");
}
LayoutLoadResult AquariumRoomStore::load() const {
    auto primary=read(path_), backup=read(path_.string()+".bak"), temporary=read(path_.string()+".tmp");
    for(const auto* entry:{&primary,&backup,&temporary})
        if(entry->status==LayoutLoadStatus::NewerVersion) return *entry;
    if(primary.document) return primary;
    if(backup.document) {backup.diagnostic="Recovered room backup"; return backup;}
    // An unpromoted draft is never authoritative. Keep it recoverable, not live.
    return primary;
}
bool AquariumRoomStore::save(const BuildingLayout& document,std::string& error) const {
    const fs::path temporary=path_.string()+".tmp", backup=path_.string()+".bak";
    try {
        const auto text=serializeBuildingLayout(document);
        if (exists() && !load().document) throw std::runtime_error("Unrecoverable/newer room save is read-only");
        fs::create_directories(path_.parent_path());
        {
            std::ofstream out(temporary,std::ios::binary|std::ios::trunc);
            out<<text; out.flush();
            if(!out) throw std::runtime_error("Room save write failed");
        }
        sync(temporary);
        const auto verified=read(temporary);
        if(!verified.document || serializeBuildingLayout(*verified.document)!=text)
            throw std::runtime_error("Room save read-back failed");
        if(read(path_).document) {
            // Stage the backup too; a failed copy must not destroy the last good backup.
            const fs::path staged=path_.string()+".bak.tmp";
            fs::copy_file(path_,staged,fs::copy_options::overwrite_existing);
            sync(staged); fs::rename(staged,backup);
        } else if(!read(backup).document) {
            fs::copy_file(temporary,backup,fs::copy_options::overwrite_existing); sync(backup);
        }
        if(fs::exists(path_) && !read(path_).document)
            fs::copy_file(path_,path_.string()+".invalid",fs::copy_options::skip_existing);
        fs::rename(temporary,path_); // atomic replacement on the supported macOS runtime
        // Promotion is the commit point. A later directory-sync error cannot roll it back.
        try {sync(path_.parent_path());} catch(...) {}
        error.clear(); return true;
    } catch(const std::exception& e) {error=e.what(); return false;}
}
}
