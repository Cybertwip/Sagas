#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace sagas {

struct Vec2 {
    float x{}, y{};
    friend constexpr Vec2 operator+(Vec2 a, Vec2 b) { return {a.x + b.x, a.y + b.y}; }
    friend constexpr Vec2 operator*(Vec2 a, float n) { return {a.x * n, a.y * n}; }
};

struct Vec3 { float x{}, y{}, z{}; };
struct Color { std::uint8_t r{}, g{}, b{}, a{255}; };
struct TriangleVertex { Vec2 position{}; Color color{255,255,255,255}; Vec2 uv{}; };
struct RasterImage {
    int width{}, height{};
    std::vector<std::uint8_t> rgba;
};

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
    std::mutex mutex_;
    std::unordered_map<std::string, std::weak_ptr<const std::vector<std::byte>>> blobs_;
};

struct Body { Vec2 position{}, velocity{}; Vec2 half_extent{0.5f, 0.5f}; bool grounded{}; };
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

enum class Action : std::uint8_t { Accept, Cancel, Skip, Up, Down, Left, Right, Quit };
struct InputState {
    bool accept_pressed{}, cancel_pressed{}, skip_pressed{}, start_pressed{}, quit{};
    bool up_pressed{}, down_pressed{}, left_pressed{}, right_pressed{};
    bool up{}, down{}, left{}, right{};
    bool jump_pressed{}, shield_held{}, back_pressed{};
    bool jump_released{};
    bool attack_pressed{}, attack_released{}, grab_pressed{}, shield_pressed{};
    bool tap_jump{true};
    bool pointer_moved{}, pointer_pressed{}, pointer_released{}, pointer_held{};
    float pointer_x{},pointer_y{};
    float stick_x{}, stick_y{};
    // Keep held controls between simulation ticks; consume edges once.
    void clear_edges() noexcept;
    void latch_edges(const InputState& previous) noexcept;
    [[nodiscard]] bool pressed(Action action) const noexcept;
};

} // namespace sagas
