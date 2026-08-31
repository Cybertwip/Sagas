#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

struct SDL_Renderer;
struct SDL_Texture;
struct SDL_Window;
struct SDL_AudioStream;

namespace sagas {

struct Vec2 {
    float x{}, y{};
    friend constexpr Vec2 operator+(Vec2 a, Vec2 b) { return {a.x + b.x, a.y + b.y}; }
    friend constexpr Vec2 operator*(Vec2 a, float n) { return {a.x * n, a.y * n}; }
};

struct Color { std::uint8_t r{}, g{}, b{}, a{255}; };
struct TriangleVertex { Vec2 position{}; Color color{255,255,255,255}; };

// Repository pattern: all game data is addressed by logical paths and read lazily.
class AssetRepository final {
public:
    explicit AssetRepository(std::filesystem::path root);
    [[nodiscard]] const std::filesystem::path& root() const noexcept { return root_; }
    [[nodiscard]] std::filesystem::path path(std::string_view logical) const;
    [[nodiscard]] std::shared_ptr<const std::vector<std::byte>> blob(std::string_view logical);
    [[nodiscard]] bool exists(std::string_view logical) const;
private:
    std::filesystem::path root_;
    std::unordered_map<std::string, std::weak_ptr<const std::vector<std::byte>>> blobs_;
};

struct Body { Vec2 position{}, velocity{}; Vec2 half_extent{0.5f, 0.5f}; bool grounded{}; };

// Strategy-friendly fixed-step physics; gameplay can replace collision policies later.
class PhysicsWorld final {
public:
    void set_gravity(Vec2 gravity) noexcept { gravity_ = gravity; }
    void set_ground(float y) noexcept { ground_y_ = y; }
    void step(std::span<Body> bodies, float seconds) const noexcept;
private:
    Vec2 gravity_{0.0f, 32.0f};
    float ground_y_{220.0f};
};

struct Keyframe { float time{}, value{}; };
class AnimationClip final {
public:
    AnimationClip() = default;
    AnimationClip(std::initializer_list<Keyframe> keys) : keys_(keys) {}
    [[nodiscard]] float sample(float time) const noexcept;
private:
    std::vector<Keyframe> keys_;
};

enum class Action : std::uint8_t { Accept, Cancel, Skip, Quit };
struct InputState {
    bool accept_pressed{}, cancel_pressed{}, skip_pressed{}, quit{};
    [[nodiscard]] bool pressed(Action action) const noexcept;
};

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

class AudioEngine final {
public:
    explicit AudioEngine(AssetRepository& assets);
    ~AudioEngine();
    AudioEngine(const AudioEngine&) = delete;
    AudioEngine& operator=(const AudioEngine&) = delete;
    void play(std::string_view logical, float gain = 1.0f);
    void stop();
private:
    AssetRepository& assets_;
    SDL_AudioStream* stream_{};
};

struct Services { AssetRepository& assets; RenderEngine& render; AudioEngine& audio; PhysicsWorld& physics; };

// State pattern: each original scene owns only its timing and presentation rules.
class Scene {
public:
    virtual ~Scene() = default;
    virtual void enter(Services&) {}
    virtual void update(Services&, const InputState&, float fixed_seconds) = 0;
    virtual void draw(Services&) = 0;
    [[nodiscard]] virtual std::unique_ptr<Scene> next() { return {}; }
};

class SceneMachine final {
public:
    SceneMachine(std::unique_ptr<Scene> initial, Services& services);
    void update(const InputState& input, float fixed_seconds);
    void draw();
private:
    Services& services_;
    std::unique_ptr<Scene> scene_;
};

struct ApplicationOptions {
    std::filesystem::path asset_root{SAGAS_DEFAULT_ASSET_ROOT};
    bool start_at_title{};
    bool headless{};
    int frame_limit{};
};

// Facade pattern: lifecycle, platform strategy, systems, and scene state are hidden here.
class Application final {
public:
    explicit Application(ApplicationOptions options);
    ~Application();
    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;
    int run();
private:
    InputState poll_input();
    ApplicationOptions options_;
    SDL_Window* window_{};
    SDL_Renderer* renderer_{};
    std::unique_ptr<AssetRepository> assets_;
    std::unique_ptr<RenderEngine> render_;
    std::unique_ptr<AudioEngine> audio_;
    PhysicsWorld physics_;
    std::unique_ptr<Services> services_;
    std::unique_ptr<SceneMachine> scenes_;
};

[[nodiscard]] std::unique_ptr<Scene> make_startup_scene();
[[nodiscard]] std::unique_ptr<Scene> make_opening_scene();
[[nodiscard]] std::unique_ptr<Scene> make_title_scene();

} // namespace sagas
