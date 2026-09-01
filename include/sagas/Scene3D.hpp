#pragma once

#include <sagas/N64.hpp>
#include <sagas/Lighting.hpp>
#include <sagas/Render.hpp>

namespace sagas {

struct Camera3D {
    Vec3 eye{0, 0, 1000};
    Vec3 at{};
    Vec3 up{0, 1, 0};
    float fov_y{45}, near_plane{16}, far_plane{65536};
};

enum class GeometryLayout { Direct, DisplayListLinks };

struct Model3D {
    std::vector<n64::Node> nodes;
    std::vector<n64::Mesh> meshes;
    std::vector<std::optional<n64::Address>> animation;
    Vec3 position{};
    Vec3 rotation{};
    Vec3 scale{1,1,1};
};

// Builder pattern: converts reloc symbols into immutable renderable models.
class Scene3DLoader final {
public:
    explicit Scene3DLoader(n64::RelocArchive& archive) : archive_(archive) {}
    [[nodiscard]] Model3D model(std::string_view descriptor, std::string_view animation = {},
                                GeometryLayout layout = GeometryLayout::DisplayListLinks);
    [[nodiscard]] Model3D display_list(std::string_view symbol,
                                      GeometryLayout layout = GeometryLayout::Direct);
    [[nodiscard]] Camera3D camera(std::string_view animation, float frame,
                                  Camera3D initial = {});
private:
    n64::RelocArchive& archive_;
};

class Scene3DRenderer final {
public:
    explicit Scene3DRenderer(n64::RelocArchive& archive) : animation_(archive) {}
    void draw(RenderEngine& render, const Model3D& model, const Camera3D& camera,
              float frame, Color tint = {255,255,255,255}, LightingRig lights = {});
private:
    n64::AnimationDecoder animation_;
};

} // namespace sagas
