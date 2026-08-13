#include "mapmaker/app/AutosaveRecovery.hpp"

#include <filesystem>
#include <iostream>
#include <stdexcept>

int main() {
    const auto directory = std::filesystem::temp_directory_path() / "pr_map_maker_autosave_test";
    std::filesystem::remove_all(directory);
    try {
        pr::mapmaker::AutosaveRecovery autosave(directory, std::chrono::seconds(60));
        auto document = pr::mapmaker::OwmapDocument::create(2, 2, 16.0f);
        document.heightAt(1, 1) = 9;
        std::string error;
        if (!autosave.writeNow("inside/house", document, &error)) {
            throw std::runtime_error("autosave should succeed: " + error);
        }
        const auto loaded = pr::mapmaker::OwmapDocument::load(autosave.pathFor("inside/house"));
        if (loaded.heightAt(1, 1) != 9) throw std::runtime_error("recovery snapshot should decode");
        if (!autosave.remove("inside/house")) throw std::runtime_error("saved recovery should be removable");

        pr::mapmaker::AutosaveRecovery multi(directory, std::chrono::seconds(0));
        auto second = pr::mapmaker::OwmapDocument::create(2, 2, 16.0f);
        second.heightAt(0, 0) = 4;
        if (!multi.writeIfDue("outside", document, true, &error) ||
            !multi.writeIfDue("inside", second, true, &error)) {
            throw std::runtime_error("each dirty source needs an independent autosave deadline");
        }
        if (pr::mapmaker::OwmapDocument::load(multi.pathFor("outside")).heightAt(1, 1) != 9 ||
            pr::mapmaker::OwmapDocument::load(multi.pathFor("inside")).heightAt(0, 0) != 4) {
            throw std::runtime_error("autosaving one source must not suppress another source");
        }
        std::filesystem::remove_all(directory);
        std::cout << "autosave_recovery_tests: PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::filesystem::remove_all(directory);
        std::cerr << "autosave_recovery_tests: FAIL: " << error.what() << '\n';
        return 1;
    }
}
