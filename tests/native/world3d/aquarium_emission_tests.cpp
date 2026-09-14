#include "gameplay/world3d/aquarium/rendering/AquariumPokemonEmission.hpp"
#include "gameplay/attend/rendering/AttendPokemonModel.hpp"

#include <filesystem>
#include <chrono>
#include <fstream>
#include <stdexcept>

void runAquariumEmissionTests() {
    namespace aq = pr::gameplay::world3d::aquarium;
    namespace render = aq::rendering;
    namespace attend = pr::gameplay::attend::rendering;
    using Role = render::AquariumPokemonEmission;
    const auto require = [](bool ok, const char* message) {
        if (!ok) throw std::runtime_error(message);
    };
    require(render::aquariumEmissionPulse(0)[0]<0 && render::aquariumEmissionPulse(5)[0]>1,
        "pulse must include genuinely dark and fully lit holds");
    require(render::aquariumEmissionPulse(3.3)[0]<render::aquariumEmissionPulse(3.9)[0] &&
        render::aquariumEmissionPulse(6.5)[0]<render::aquariumEmissionPulse(7.1)[0] &&
        render::aquariumEmissionPulse(6.5)[1]==1 &&
        render::aquariumEmissionPulse(7.4)==render::aquariumEmissionPulse(0),
        "both pulse fronts must spread outward and repeat seamlessly");
    require(aq::AquariumPokemonEmissionConfig{}.fog_retention == 0.4f,
        "post-fog bulb retention must remain visible without fully replacing water fog");
    for (const char* file : {"pm0170_00_Chinchou.glbz", "pm0171_00_Lanturn.glbz"}) {
        const auto path = std::filesystem::path(PR_SOURCE_DIR) /
            "assets/pokemon_attend/pokemon_models" / file;
        std::string error;
        const auto model = attend::loadAttendPokemonModelShared(path.string(), &error);
        require(model && model->valid, "aquarium emission fixture could not load its bulb model");
        unsigned bulbs = 0, shells = 0, ordinary = 0;
        for (const auto& material : model->materials) {
            const auto role = render::aquariumPokemonEmission(path.string(), material.name);
            bulbs += role == Role::Bulb;
            shells += role == Role::GlowShell;
            ordinary += role == Role::None;
            if (material.material_role != attend::AttendMaterialRole::None || material.pokemon_eye)
                require(role == Role::None, "eye/mouth mask was incorrectly treated as aquarium emission");
        }
        require(bulbs == 2 && shells == 2 && ordinary > 0,
            "normal/shiny bulb and glow-shell materials were not isolated from the body");
    }
    require(render::aquariumPokemonEmission("pm0457_00_Lumineon.glbz", "BodyNeolantInc") == Role::None,
        "shared export material names leaked emission into an unrelated species");
    require(render::aquariumPokemonEmission("pm0171_00_Lanturn.glbz", "BodyTattu00") == Role::None,
        "Lanturn body should remain ordinarily lit");
    {
        const auto path=std::filesystem::path(PR_SOURCE_DIR)/
            "assets/pokemon_attend/pokemon_models/pm0382_00_Kyogre.glbz";
        std::string error;
        const auto model=attend::loadAttendPokemonModelShared(path.string(),&error);
        require(model && model->valid,"Kyogre emission fixture could not load");
        unsigned markings=0;
        for(const auto& material:model->materials) {
            const bool stripe=material.name=="BodyANeolant_Inc" || material.name=="BodyBNeolant_Inc";
            const auto role=render::aquariumPokemonEmission(path.string(),material.name);
            require(role==(stripe?Role::GlowShell:Role::None),
                "Kyogre emission leaked outside the red-line overlay materials");
            markings+=stripe;
        }
        require(markings>=2,"Kyogre red-line materials were not found in the actual asset");
        require(render::aquariumPokemonEmission("pm0457_00_Lumineon.glbz","BodyANeolant_Inc")==Role::None,
            "Kyogre's material names enabled emission in another species");
    }
    aq::AquariumPokemonPresentationConfig dark;
    dark.brightness = dark.pokemon_brightness = dark.ambient = 0.01f;
    dark.tint = {0.1f, 0.3f, 0.6f};
    const auto bulb = render::aquariumEmissionPresentation(dark, Role::Bulb);
    const auto shell = render::aquariumEmissionPresentation(dark, Role::GlowShell);
    const auto body = render::aquariumEmissionPresentation(dark, Role::None);
    require(bulb.brightness == 1.0f && bulb.pokemon_brightness == 1.0f && bulb.ambient == 1.0f &&
            bulb.directional == 0 && bulb.form_shadow == 0 && bulb.tint == std::array<float,3>{1,1,1},
        "bulb emission must bypass exhibit dimming, tint and directional/form shadows");
    require(shell.brightness > 0 && shell.brightness < bulb.brightness,
        "additive glow shell must use a restrained self-lit intensity");
    require(body.brightness == dark.brightness && body.tint == dark.tint && body.ambient == dark.ambient &&
            dark.brightness == 0.01f,
        "emission changed ordinary materials or mutated shared exhibit presentation");

    const auto root = std::filesystem::temp_directory_path() / ("aquarium_emission_" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    struct Cleanup {
        std::filesystem::path root;
        ~Cleanup() { std::error_code ec; std::filesystem::remove_all(root, ec); }
    } cleanup{root};
    const auto config = root / "config/gameplay/world3d/aquariums.json";
    std::filesystem::create_directories(config.parent_path());
    {
        std::ofstream file(config);
        file << R"({"pokemonPresentation":{"emission":{
            "bulbBrightness":1.5,"haloBrightness":1.1,"fogRetention":0.75}},"maps":[
            {"mapId":"inherit"},
            {"mapId":"partial","pokemonPresentation":{"emission":{"haloBrightness":2}}},
            {"mapId":"limits","pokemonPresentation":{"emission":{
                "bulbBrightness":100,"haloBrightness":-2,"fogRetention":5}}},
            {"mapId":"invalid","pokemonPresentation":{"emission":{
                "bulbBrightness":"nan","haloBrightness":"inf","fogRetention":false}}}
        ]})";
    }
    std::string error;
    const auto catalog = aq::loadAquariumCatalog(root.string(), &error);
    require(error.empty() && catalog.maps.size() == 4, "emission JSON fixture failed to load");
    const auto& inherited = catalog.maps[0].pokemon_presentation;
    require(inherited.emission.fog_retention == 0.75f &&
            render::aquariumEmissionPresentation(inherited, Role::Bulb).brightness == 1.5f &&
            render::aquariumEmissionPresentation(inherited, Role::GlowShell).brightness == 1.1f,
        "root emission JSON must reach actual bulb and halo presentation");
    const auto& partial = catalog.maps[1].pokemon_presentation.emission;
    require(partial.bulb_brightness == 1.5f && partial.halo_brightness == 2 && partial.fog_retention == 0.75f,
        "partial map emission override must preserve inherited fields");
    const auto& limits = catalog.maps[2].pokemon_presentation.emission;
    require(limits.bulb_brightness == 4 && limits.halo_brightness == 0 && limits.fog_retention == 1,
        "emission settings must clamp to documented ranges");
    const auto& invalid = catalog.maps[3].pokemon_presentation.emission;
    require(invalid.bulb_brightness == 1.5f && invalid.halo_brightness == 1.1f && invalid.fog_retention == 0.75f,
        "invalid/nonfinite emission settings must preserve inherited values");
}
