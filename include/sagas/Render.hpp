#pragma once

#include <sagas/Core.hpp>
#include <sagas/Lighting.hpp>

#include <filesystem>

struct SDL_Window;
struct SDL_GLContextState;

namespace sagas {

struct ForwardVertex {
    Vec3 position{}; // camera space, +Z forward
    Vec3 normal{};
    Color color{255,255,255,255};
    Vec2 uv{};
};

struct ForwardMaterial {
    std::shared_ptr<const RasterImage> texture;
    LightingRig lights{};
    Color material_diffuse{255,255,255,255};
    Color material_ambient{255,255,255,255};
    float fov_y{45}, near_plane{16}, far_plane{65536};
    std::uint8_t texture_mode_s{}, texture_mode_t{};
    std::uint8_t texture_mask_s{}, texture_mask_t{};
    std::uint16_t texture_window_s{}, texture_window_t{};
    bool lit{}, translucent{};
};

class RenderEngine final {
public:
    RenderEngine(SDL_Window* window, AssetRepository& assets);
    ~RenderEngine();
    RenderEngine(const RenderEngine&) = delete;
    RenderEngine& operator=(const RenderEngine&) = delete;
    void begin(Color clear);
    void sprite(std::string_view logical, Vec2 center, Vec2 scale = {1, 1},
                Color tint = {255, 255, 255, 255});
    void sprite_at(std::string_view logical, Vec2 top_left, Vec2 scale = {1, 1},
                   Color tint = {255, 255, 255, 255});
    void fill(float x, float y, float w, float h, Color color);
    void triangles(std::span<const TriangleVertex> vertices,
                   const std::shared_ptr<const RasterImage>& image = {});
    void prepare_forward_shadows(std::span<const ForwardVertex> vertices, Vec3 light_direction);
    void forward(std::span<const ForwardVertex> vertices, const ForwardMaterial& material);
    void clear_depth();
    void request_capture(std::filesystem::path path);
    void end();
private:
    struct Texture { std::uint32_t handle{}; float width{}, height{}; };
    Texture& texture(std::string_view logical);
    std::uint32_t raster_texture(const std::shared_ptr<const RasterImage>& image);
    void draw_2d(std::span<const TriangleVertex> vertices, std::uint32_t texture);
    SDL_Window* window_{};
    SDL_GLContextState* context_{};
    AssetRepository& assets_;
    std::unordered_map<std::string, Texture> textures_;
    std::unordered_map<const RasterImage*, std::uint32_t> raster_textures_;
    std::uint32_t program_2d_{}, program_forward_{}, program_shadow_{};
    std::uint32_t vao_2d_{}, vbo_2d_{}, vao_forward_{}, vbo_forward_{};
    std::uint32_t shadow_framebuffer_{}, shadow_texture_{};
    Vec3 shadow_right_{1,0,0}, shadow_up_{0,1,0}, shadow_forward_{0,0,1};
    Vec3 shadow_min_{-1,-1,-1}, shadow_max_{1,1,1};
    bool shadows_ready_{};
    int viewport_x_{}, viewport_y_{}, viewport_width_{320}, viewport_height_{240};
    std::filesystem::path capture_path_;
};

} // namespace sagas
