#pragma once
#include <sagas/Fighter.hpp>

#include <sagas/N64.hpp>
#include <sagas/Lighting.hpp>
#include <sagas/Render.hpp>

#include <array>

namespace sagas {

struct Camera3D {
    Vec3 eye{0, 0, 1000};
    Vec3 at{};
    Vec3 up{0, 1, 0};
    float fov_y{45}, near_plane{16}, far_plane{65536};
    std::array<float,4> viewport{10,10,300,220};
    float aspect{4.0f/3.0f};
};

enum class GeometryLayout { Direct, DisplayListLinks, JointPairs };

struct Model3D {
    // Use FTAnimDesc.flags semantics: 0x80000000 is XRotN (rendered),
    // 0x40000000 is TransN (detached motion). ftdef.h names are reversed.
    enum class FighterWrapper { None, TransN, XRotN };
    std::vector<n64::Node> nodes;
    std::vector<unsigned> source_joint_ids;
    std::vector<n64::Mesh> meshes;
    // Fighter JointPairs slot 0 is drawn in the parent matrix; slot 1 is
    // drawn after applying the current joint matrix.
    std::vector<n64::Mesh> parent_meshes;
    std::vector<std::vector<n64::Material>> materials;
    std::vector<std::vector<std::optional<n64::Address>>> material_animation;
    std::vector<std::optional<n64::Address>> animation;
    n64::Node fighter_root{};
    std::optional<n64::Address> fighter_root_animation;
    FighterWrapper fighter_wrapper{FighterWrapper::None};
    bool fighter_animation{};
    bool is_fighter{};
    bool receive_lighting{true};
    bool emit_spotlight{};
    bool additive{};
    float material_animation_start{};
    Vec3 position{};
    Vec3 rotation{};
    Vec3 scale{1,1,1};
    std::optional<std::array<float,16>> root_transform;
};

struct Stage3D {
    std::array<Model3D,4> layers;
    Vec3 movie_player1{}, movie_player2{}, movie_player3{};
    std::array<Vec3,4> player_spawns{};
    std::vector<CollisionSegment> collision;
    std::array<float,4> blast_bounds{5000,-4000,6000,-6000}; // top, bottom, right, left
};

// Builder pattern: converts reloc symbols into immutable renderable models.
class Scene3DLoader final {
public:
    explicit Scene3DLoader(n64::RelocArchive& archive) : archive_(archive) {}
    [[nodiscard]] Model3D model(std::string_view descriptor, std::string_view animation = {},
                                GeometryLayout layout = GeometryLayout::DisplayListLinks,
                                std::string_view materials = {},
                                std::string_view material_animation = {});
    [[nodiscard]] Stage3D stage(std::string_view header);
    [[nodiscard]] Model3D model(n64::Address descriptor,
                                std::optional<n64::Address> animation, GeometryLayout layout,
                                std::optional<n64::Address> materials,
                                std::optional<n64::Address> material_animation);
    [[nodiscard]] Model3D fighter_model(std::string_view descriptor,
                                        GeometryLayout layout = GeometryLayout::Direct,
                                        std::array<std::uint32_t,2> setup_parts = {
                                            0xffffffffU, 0xffffffffU},
                                        std::uint32_t animation_flags = 0, unsigned costume = 0);
    [[nodiscard]] Model3D display_list(std::string_view symbol,
                                      GeometryLayout layout = GeometryLayout::Direct,
                                      std::string_view materials = {},
                                      std::string_view material_animation = {});
    [[nodiscard]] Camera3D camera(std::string_view animation, float frame,
                                  Camera3D initial = {});
private:
    n64::RelocArchive& archive_;
};

class Scene3DRenderer final {
public:
    explicit Scene3DRenderer(n64::RelocArchive& archive) : animation_(archive) {}
    void begin();
    void draw(RenderEngine& render, const Model3D& model, const Camera3D& camera,
              float frame, Color tint = {255,255,255,255}, LightingRig lights = {});
    [[nodiscard]] Model3D placed_at_joint(const Model3D& model, float model_frame,
                                          const Model3D& carrier, float carrier_frame,
                                          std::size_t carrier_joint);
    [[nodiscard]] Vec3 joint_point(const Model3D& model, float frame, unsigned joint, Vec3 offset = {});
    [[nodiscard]] Vec3 fighter_position(const Model3D& model, float frame);
    void flush(RenderEngine& render);
    void end(RenderEngine& render);
private:
    struct ProjectedVertex {
        Vec3 position{};
        Color color{255,255,255,255};
        Vec2 uv{};
        Vec3 normal{};
    };
    struct ProjectedTriangle {
        std::array<ProjectedVertex,3> points;
        std::shared_ptr<const RasterImage> texture;
        std::uint8_t texture_mode_s{}, texture_mode_t{};
        std::uint8_t texture_mask_s{}, texture_mask_t{};
        std::uint16_t texture_window_s{}, texture_window_t{};
        LightingRig lights{};
        std::optional<Color> material_light1;
        std::optional<Color> material_light2;
        float fov_y{45}, near_plane{16}, far_plane{65536};
        bool lit{}, translucent{}, additive{};
        N64RenderState rdp;
        std::array<float,4> viewport{10,10,300,220};
        float aspect{4.0f/3.0f};
    };
    n64::AnimationDecoder animation_;
    std::vector<ProjectedTriangle> triangles_;
    bool batching_{};
};

} // namespace sagas
