#include <sagas/Render.hpp>

#include <SDL3/SDL.h>
#include <png.h>

#include <stdexcept>

namespace sagas {
namespace {
[[noreturn]] void fail(std::string message) {
    if (const char* detail = SDL_GetError(); detail && *detail) message += ": " + std::string(detail);
    throw std::runtime_error(std::move(message));
}
}

RenderEngine::RenderEngine(SDL_Window* window, SDL_Renderer* renderer, AssetRepository& assets)
    : renderer_(renderer), assets_(assets) {
    (void)window;
    if (!SDL_SetRenderLogicalPresentation(renderer_, 320, 240, SDL_LOGICAL_PRESENTATION_LETTERBOX)) fail("logical renderer setup failed");
}
RenderEngine::~RenderEngine() {
    for (auto& [_, texture] : textures_) SDL_DestroyTexture(texture.handle);
    for (auto& [_, texture] : raster_textures_) SDL_DestroyTexture(texture);
}
RenderEngine::Texture& RenderEngine::texture(std::string_view logical) {
    const std::string key(logical);
    if (auto found = textures_.find(key); found != textures_.end()) return found->second;
    png_image image{};
    image.version = PNG_IMAGE_VERSION;
    const auto file = assets_.path(logical).string();
    if (!png_image_begin_read_from_file(&image, file.c_str())) throw std::runtime_error("PNG read failed: " + file);
    image.format = PNG_FORMAT_RGBA;
    std::vector<std::uint8_t> pixels(PNG_IMAGE_SIZE(image));
    if (!png_image_finish_read(&image, nullptr, pixels.data(), 0, nullptr)) {
        png_image_free(&image);
        throw std::runtime_error("PNG decode failed: " + file);
    }
    SDL_Texture* handle = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC,
                                            static_cast<int>(image.width), static_cast<int>(image.height));
    if (!handle) fail("texture creation failed");
    SDL_UpdateTexture(handle, nullptr, pixels.data(), static_cast<int>(image.width * 4));
    SDL_SetTextureScaleMode(handle, SDL_SCALEMODE_NEAREST);
    SDL_SetTextureBlendMode(handle, SDL_BLENDMODE_BLEND);
    auto [inserted, _] = textures_.emplace(key, Texture{handle, static_cast<float>(image.width), static_cast<float>(image.height)});
    png_image_free(&image);
    return inserted->second;
}
void RenderEngine::begin(Color clear) { SDL_SetRenderDrawColor(renderer_, clear.r, clear.g, clear.b, clear.a); SDL_RenderClear(renderer_); }
void RenderEngine::sprite(std::string_view logical, Vec2 center, Vec2 scale, Color tint) {
    auto& source = texture(logical);
    SDL_SetTextureColorMod(source.handle, tint.r, tint.g, tint.b);
    SDL_SetTextureAlphaMod(source.handle, tint.a);
    const SDL_FRect destination{center.x - source.width * scale.x * 0.5f, center.y - source.height * scale.y * 0.5f,
                                source.width * scale.x, source.height * scale.y};
    SDL_RenderTexture(renderer_, source.handle, nullptr, &destination);
}
void RenderEngine::fill(float x, float y, float w, float h, Color color) {
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer_, color.r, color.g, color.b, color.a);
    const SDL_FRect rect{x, y, w, h};
    SDL_RenderFillRect(renderer_, &rect);
}
void RenderEngine::triangles(std::span<const TriangleVertex> vertices,
                             const std::shared_ptr<const RasterImage>& image) {
    if (vertices.empty()) return;
    std::vector<SDL_Vertex> native;
    native.reserve(vertices.size());
    for (const auto& vertex : vertices)
        native.push_back({{vertex.position.x, vertex.position.y},
                          {vertex.color.r / 255.0f, vertex.color.g / 255.0f,
                           vertex.color.b / 255.0f, vertex.color.a / 255.0f}, {vertex.uv.x, vertex.uv.y}});
    SDL_Texture* texture_handle{};
    if (image && image->width > 0 && image->height > 0 && !image->rgba.empty()) {
        if (const auto found = raster_textures_.find(image.get()); found != raster_textures_.end()) {
            texture_handle = found->second;
        } else {
            texture_handle = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC,
                                               image->width, image->height);
            if (!texture_handle) fail("N64 texture creation failed");
            SDL_UpdateTexture(texture_handle, nullptr, image->rgba.data(), image->width * 4);
            SDL_SetTextureScaleMode(texture_handle, SDL_SCALEMODE_NEAREST);
            SDL_SetTextureBlendMode(texture_handle, SDL_BLENDMODE_BLEND);
            raster_textures_.emplace(image.get(), texture_handle);
        }
    }
    SDL_RenderGeometry(renderer_, texture_handle, native.data(), static_cast<int>(native.size()), nullptr, 0);
}
void RenderEngine::request_capture(std::filesystem::path path) { capture_path_ = std::move(path); }
void RenderEngine::end() {
    if (!capture_path_.empty()) {
        SDL_Surface* native = SDL_RenderReadPixels(renderer_, nullptr);
        if (!native) fail("frame capture failed");
        SDL_Surface* rgba = SDL_ConvertSurface(native, SDL_PIXELFORMAT_RGBA32);
        SDL_DestroySurface(native);
        if (!rgba) fail("frame capture conversion failed");
        png_image image{};
        image.version = PNG_IMAGE_VERSION;
        image.width = static_cast<png_uint_32>(rgba->w);
        image.height = static_cast<png_uint_32>(rgba->h);
        image.format = PNG_FORMAT_RGBA;
        const auto filename = capture_path_.string();
        if (!png_image_write_to_file(&image, filename.c_str(), 0, rgba->pixels, rgba->pitch, nullptr)) {
            SDL_DestroySurface(rgba);
            throw std::runtime_error("PNG capture write failed: " + filename);
        }
        SDL_DestroySurface(rgba);
        capture_path_.clear();
    }
    SDL_RenderPresent(renderer_);
}

} // namespace sagas
