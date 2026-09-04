#pragma once

#include <sagas/Scene3D.hpp>

#include <future>
#include <unordered_map>

namespace sagas {

struct SceneRenderCue {
    std::string resource;
    std::string camera;
    std::uint32_t order{};
    Color tint{255,255,255,255};
};

struct SceneTimelineSegment {
    std::string name;
    std::string renderer;
    std::string argument;
    std::string bundle;
    std::uint32_t duration{}, preload_lead{};
    Vec2 scale{1,1};
    Color color{0,0,0,255};
    std::vector<SceneRenderCue> cues;
};

// Manifest + Repository + Factory: scene code asks for stable logical keys;
// the binary manifest owns all relocation links and the factory decodes only
// the bundle needed by the current timeline segment.
class SceneResourceManager final {
public:
    explicit SceneResourceManager(AssetRepository& assets);
    ~SceneResourceManager();
    SceneResourceManager(const SceneResourceManager&) = delete;
    SceneResourceManager& operator=(const SceneResourceManager&) = delete;

    void load_manifest(std::string_view logical);
    void prefetch(std::string_view bundle);
    void activate(std::string_view bundle);
    [[nodiscard]] bool ready(std::string_view bundle) const;
    [[nodiscard]] bool loaded(std::string_view bundle) const;
    [[nodiscard]] const Model3D& model(std::string_view key) const;
    [[nodiscard]] std::span<const SceneTimelineSegment> timeline() const noexcept { return timeline_; }
    void release(std::string_view bundle);
    void clear();

    [[nodiscard]] n64::RelocArchive& archive() noexcept { return archive_; }
    [[nodiscard]] Scene3DLoader& loader() noexcept { return loader_; }

private:
    enum class Kind : std::uint8_t { Model, DisplayList, Fighter, Transition };
    struct Descriptor {
        std::string key, descriptor, animation, materials, material_animation, dependency;
        Kind kind{};
        GeometryLayout layout{};
        Model3D::FighterWrapper wrapper{};
        bool unlit{};
        bool emit_spotlight{};
        std::uint32_t animation_file{};
        std::array<std::uint32_t,2> setup_parts{0xffffffffU, 0xffffffffU};
        float transition_frame{}, material_start{};
        Vec3 position{};
    };
    struct Bundle { std::string name; std::vector<Descriptor> resources; };
    struct BuiltBundle { std::string name; std::unordered_map<std::string, Model3D> models; };

    [[nodiscard]] BuiltBundle build(const Bundle& bundle,
                                    std::unordered_map<std::string, Model3D> dependencies);
    [[nodiscard]] Model3D build_model(const Descriptor& descriptor,
                                      const std::unordered_map<std::string, Model3D>& available);
    void merge(BuiltBundle bundle);
    void poll();

    AssetRepository& assets_;
    n64::RelocArchive archive_;
    Scene3DLoader loader_;
    std::unordered_map<std::string, Bundle> bundles_;
    std::unordered_map<std::string, Model3D> models_;
    std::unordered_map<std::string, std::string> owners_;
    std::unordered_map<std::string, bool> loaded_bundles_;
    std::vector<SceneTimelineSegment> timeline_;
    std::future<BuiltBundle> pending_;
    std::string pending_name_;
    std::string manifest_;
};

} // namespace sagas
