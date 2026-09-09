#pragma once

#include <sagas/Core.hpp>

#include <array>
#include <cstdint>
#include <string_view>
#include <functional>
#include <optional>

namespace sagas {

// Remix ftphysics.c, without GObj: one 60 Hz tick is one call.  Units are
// the original fighter/map units (Mario's run speed is 44 per tick).
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
    float width{150.0f};
    float run_speed{44}, jump_vel_x{.35f}, size{1.12f}, weight{1};
    int knee_bend{3}, jumps_max{2};
    float jump_height_mul{.7f},jump_height_base{26},aerial_vel_x{.35f},aerial_height{.9f};
};

enum class FighterKind : std::uint8_t {
    Luigi, Mario, Donkey, Link, Samus, Captain,
    Ness, Yoshi, Kirby, Fox, Pikachu, Purin, Count
};

enum class FighterStatus : std::uint8_t { Wait, Turn, Crouch, CrouchWait, CrouchEnd, Walk, Dash, Run, RunBrake, KneeBend, Jump, Fall, Land, Attack, Special, Hitstun, Tumble, DownBounce, DownWait, DownStand, DownRoll, DownAttack, Shield, ShieldRelease, ShieldRoll, CliffCatch, CliffWait, CliffClimb, Catch, CatchWait, Captured, Throw, KO };

[[nodiscard]] constexpr bool fighter_is_down(FighterStatus status) {
    return status==FighterStatus::DownBounce || status==FighterStatus::DownWait ||
           status==FighterStatus::DownStand || status==FighterStatus::DownRoll || status==FighterStatus::DownAttack;
}

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
    Vec3 vel_damage{};
    float vel_ground{};
    float floor_friction{4};
    Vec2 floor_tangent{1,0};
    int lr{1};
    int stick_x{};
    int stick_y{};
    int tap_stick_y{255};
    int jump_frames{};
    bool jump_button{},jump_released{},short_hop{},jump_backward{},aerial_jump{};
    int jump_force{80};
    unsigned jab_stage{};
    int jab_followup_left{};
    bool jab_queued{};
    unsigned attack_motion{},attack_epoch{~0U};
    int aerial_attack{-1}, shield_tics{255};
    unsigned special_motion{},special_index{},special_phase{},special_tics{};
    bool special_projectile{},special_held{};
    Vec2 special_velocity{};
    unsigned damage_motion{};
    bool damage_tumble{};
    unsigned down_face{1},down_motion{};
    int down_wait{},down_attack_buffer{};
    bool recovery_invulnerable{};
    unsigned landing_motion{};
    float landing_speed{1};
    int capture_target{-1}, captured_by{-1}, capture_tics{};
    bool throw_backward{},turn_flipped{},turn_dash{},captured_throw{};
    float capture_rotation{};
    int smash_buffer{},buffered_smash{-1},buffered_facing{1};
    int rapid_inputs{},tap_stick_x{255};
    bool rapid_continue{};
    Vec2 cliff_edge{};
    unsigned cliff_line{};
    int cliff_wait{},cliff_cooldown{},cliff_phase{};
    bool cliff_neutral{};
    int land_frames{};
    bool grounded{true};
    bool fastfall{};
    FighterStatus status{FighterStatus::Wait};
    float damage{};
    int jumps_used{}, action_frame{}, hitlag{}, hitstun{}, invincible{}, drop_frames{};
    unsigned motion{},audio_motion{};
    int audio_frame{-1};
    bool attack_pressed{}, jump_pressed{}, shield_held{};
    unsigned hit_mask{};
    std::array<unsigned,8> hit_group_masks{};
    std::array<unsigned,8> hit_group_epochs{~0U,~0U,~0U,~0U,~0U,~0U,~0U,~0U};
    int stocks{3};
    float shield{55};
    unsigned guard_motion{};
    int shield_stun{};
};

struct CollisionSegment {
    Vec2 a{}, b{};
    unsigned type{}, flags{}; // 0 floor, 1 ceiling, 2 right wall, 3 left wall
    bool pass_through{};
    unsigned line_id{};
};

class FighterPhysics final {
public:
    using JumpMotion=std::function<std::optional<Vec3>(const FighterBody&)>;
    static bool try_ledge(FighterBody& body,Vec3 before,std::span<const CollisionSegment> stage,
                          std::span<const FighterBody> others);
    static constexpr int kStickMin = 8;
    static void apply_gravity_clamp_tvel(FighterBody& body, float gravity, float tvel) noexcept;
    static void clamp_ground_vel(FighterBody& body, float clamp) noexcept;
    static void apply_ground_friction(FighterBody& body) noexcept;
    static void apply_air_vel_x_friction(FighterBody& body) noexcept;
    static void clamp_air_vel_x_stick(FighterBody& body) noexcept;
    static void apply_air_vel_drift(FighterBody& body) noexcept;
    static void jump(FighterBody& body) noexcept;
    static void tick(FighterBody& body, float ground_y) noexcept;
    static void tick(FighterBody& body, std::span<const CollisionSegment> stage, const JumpMotion& motion = {});
};

struct AttackVolume {
    unsigned owner{};
    Vec3 position{};
    float radius{};
    int damage{}, angle{}, growth{}, weight{}, base{};
    unsigned fgm{};
    bool grab{};
    unsigned group{},epoch{~0U},element{};
    bool projectile{};
    int facing{};
};
struct FighterHit { unsigned attacker{}, defender{}; bool shield{}; unsigned fgm{},element{}; };
class FighterCombat final {
public:
    static void advance_guard(FighterBody& body,bool animation_ended);
    static void advance_down(FighterBody& body,bool attack,bool stand,bool animation_ended);
    static void advance_jab(FighterBody& body,bool pressed,bool animation_ended,bool released=false);
    static bool start_special(FighterBody& body,bool pressed);
    static void advance_special(FighterBody& body,bool pressed,bool animation_ended);
    static bool start_aerial(FighterBody& body,bool pressed);
    static bool start_grab(FighterBody& body,bool pressed);
    static bool start_dash_attack(FighterBody& body,bool pressed);
    static bool start_tilt(FighterBody& body,bool pressed);
    static void buffer_smash(FighterBody& body,bool pressed);
    static bool start_smash(FighterBody& body,bool pressed);
    [[nodiscard]] static std::vector<FighterHit> resolve(std::span<FighterBody> bodies,
                                                        std::span<const AttackVolume> attacks);
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
