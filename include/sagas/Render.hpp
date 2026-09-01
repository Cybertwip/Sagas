#pragma once

#include <sagas/Core.hpp>

struct SDL_Renderer;
struct SDL_Texture;
struct SDL_Window;

namespace sagas {

class RenderEngine final {
public:
    RenderEngine(SDL_Window* window, SDL_Renderer* renderer, AssetRepository& assets);
    ~RenderEngine();
    RenderEngine(const RenderEngine&) = delete;
    RenderEngine& operator=(const RenderEngine&) = delete;
    void begin(Color clear);
    void sprite(std::string_view logical, Vec2 center, Vec2 scale = {1, 1},
                Color tint = {255, 255, 255, 255});
    void fill(float x, float y, float w, float h, Color color);
    void triangles(std::span<const TriangleVertex> vertices);
    void end();
private:
    struct Texture { SDL_Texture* handle{}; float width{}, height{}; };
    Texture& texture(std::string_view logical);
    SDL_Renderer* renderer_{};
    AssetRepository& assets_;
    std::unordered_map<std::string, Texture> textures_;
};

} // namespace sagas
