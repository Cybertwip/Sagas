#pragma once

#include <sagas/Core.hpp>

#include <array>
#include <cstdint>
#include <string_view>

namespace sagas {

// Remix ftphysics.c, without GObj: one 60 Hz tick is one call.  Units are
// the original fighter/map units (Mario's walk speed is ~1.2).
struct FighterAttributes {
    float gravity{0.095f};
    float tvel_base{1.5f};
    float tvel_fast{2.4f};
    float jump_vel_y{2.785f};
    float walk_speed{1.2f};
    float dash_speed{1.6f};
    float traction{0.08f};
    float air_accel{0.06f};
    float air_friction{0.016f};
    float air_speed_max_x{0.83f};
    float height{18.0f};
    float width{6.0f};
};

enum class FighterKind : std::uint8_t {
    Luigi, Mario, Donkey, Link, Samus, Captain,
    Ness, Yoshi, Kirby, Fox, Pikachu, Purin, Count
};

enum class FighterStatus : std::uint8_t { Wait, Walk, Dash, KneeBend, Jump, Fall, Land };

struct FighterModelSpec {
    std::string_view descriptor;
    // Original FTAttributes.setup_parts bits. Descriptor zero is bit 31;
    // each subsequent descriptor consumes the next bit.
    std::array<std::uint32_t,2> setup_parts;
    // Yoshi and Master Hand use the two-slot fighter display-list layout;
    // the regular cast otherwise uses one direct display list per joint.
    bool joint_pairs{};
};

struct FighterBody {
    FighterKind kind{FighterKind::Mario};
    FighterAttributes attr{};
    Vec3 position{};
    Vec3 vel_air{};
    float vel_ground{};
    int lr{1};
    int stick_x{};
    int stick_y{};
    int tap_stick_y{255};
    int jump_frames{};
    int land_frames{};
    bool grounded{true};
    bool fastfall{};
    FighterStatus status{FighterStatus::Wait};
    float damage{};
};

class FighterPhysics final {
public:
    static constexpr int kStickMin = 8;
    static void apply_gravity_clamp_tvel(FighterBody& body, float gravity, float tvel) noexcept;
    static void clamp_ground_vel(FighterBody& body, float clamp) noexcept;
    static void apply_ground_friction(FighterBody& body) noexcept;
    static void apply_air_vel_x_friction(FighterBody& body) noexcept;
    static void clamp_air_vel_x_stick(FighterBody& body) noexcept;
    static void apply_air_vel_drift(FighterBody& body) noexcept;
    static void jump(FighterBody& body) noexcept;
    static void tick(FighterBody& body, float ground_y) noexcept;
};

[[nodiscard]] constexpr std::string_view fighter_kind_name(FighterKind kind) {
    constexpr std::array<std::string_view, 12> names{
        "Luigi", "Mario", "Donkey", "Link", "Samus", "Captain",
        "Ness", "Yoshi", "Kirby", "Fox", "Pikachu", "Purin"};
    const auto index = static_cast<std::size_t>(kind);
    return index < names.size() ? names[index] : "Mario";
}

[[nodiscard]] constexpr std::string_view fighter_portrait_file(FighterKind kind) {
    constexpr std::array<std::string_view, 12> files{
        "Luigi.png", "Mario.png", "Donkey.png", "Link.png", "Samus.png", "Captain.png",
        "Ness.png", "Yoshi.png", "Kirby.png", "Fox.png", "Pikachu.png", "Purin.png"};
    const auto index = static_cast<std::size_t>(kind);
    return index < files.size() ? files[index] : files[1];
}

[[nodiscard]] FighterAttributes fighter_attributes(FighterKind kind);
[[nodiscard]] FighterModelSpec fighter_model_spec(FighterKind kind) noexcept;

} // namespace sagas
