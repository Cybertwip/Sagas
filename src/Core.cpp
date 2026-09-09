#include <sagas/Core.hpp>

#include <algorithm>
#include <fstream>
#include <stdexcept>

namespace sagas {
void InputState::clear_edges() noexcept {
    accept_pressed=cancel_pressed=skip_pressed=start_pressed=quit=false;
    up_pressed=down_pressed=left_pressed=right_pressed=false;
    jump_pressed=back_pressed=jump_released=false;
    attack_pressed=attack_released=grab_pressed=shield_pressed=false;
    special_pressed=taunt_pressed=false;
    for (auto& c:controllers) {c.attack=c.attack_released=c.special=c.jump=c.jump_released=c.shield_pressed=c.grab=c.start=c.cancel=c.taunt=false;}
    pointer_moved=pointer_pressed=pointer_released=false;
}
void InputState::latch_edges(const InputState& previous) noexcept {
    special_pressed|=previous.special_pressed;taunt_pressed|=previous.taunt_pressed;
    for (unsigned i=0;i<controllers.size();++i) {
        auto& c=controllers[i];const auto& p=previous.controllers[i];
        c.attack|=p.attack;c.attack_released|=p.attack_released;c.special|=p.special;c.jump|=p.jump;c.jump_released|=p.jump_released;
        c.shield_pressed|=p.shield_pressed;c.grab|=p.grab;c.start|=p.start;c.cancel|=p.cancel;c.taunt|=p.taunt;
    }
    accept_pressed|=previous.accept_pressed; cancel_pressed|=previous.cancel_pressed;
    skip_pressed|=previous.skip_pressed; start_pressed|=previous.start_pressed;
    quit|=previous.quit; jump_pressed|=previous.jump_pressed; back_pressed|=previous.back_pressed;
    jump_released|=previous.jump_released;
    attack_pressed|=previous.attack_pressed;attack_released|=previous.attack_released;
    grab_pressed|=previous.grab_pressed;shield_pressed|=previous.shield_pressed;
    if (!pointer_moved && previous.pointer_moved) {pointer_x=previous.pointer_x;pointer_y=previous.pointer_y;}
    pointer_moved|=previous.pointer_moved;pointer_pressed|=previous.pointer_pressed;pointer_released|=previous.pointer_released;
    up_pressed|=previous.up_pressed; down_pressed|=previous.down_pressed;
    left_pressed|=previous.left_pressed; right_pressed|=previous.right_pressed;
}

AssetRepository::AssetRepository(std::filesystem::path root) : root_(std::move(root)) {
    if (!std::filesystem::exists(root_ / ".complete"))
        throw std::runtime_error("asset bundle is missing or incomplete: " + root_.string());
}
std::filesystem::path AssetRepository::path(std::string_view logical) const { return root_ / logical; }
bool AssetRepository::exists(std::string_view logical) const { return std::filesystem::is_regular_file(path(logical)); }
std::shared_ptr<const std::vector<std::byte>> AssetRepository::blob(std::string_view logical) {
    const std::scoped_lock lock(mutex_);
    const std::string key(logical);
    if (const auto found = blobs_.find(key); found != blobs_.end())
        if (auto cached = found->second.lock()) return cached;
    std::ifstream input(path(logical), std::ios::binary | std::ios::ate);
    if (!input) throw std::runtime_error("missing asset: " + path(logical).string());
    const auto size = input.tellg();
    input.seekg(0);
    auto data = std::make_shared<std::vector<std::byte>>(static_cast<std::size_t>(size));
    if (!input.read(reinterpret_cast<char*>(data->data()), size)) throw std::runtime_error("failed to read asset: " + key);
    blobs_[key] = data;
    return data;
}

void PhysicsWorld::step(std::span<Body> bodies, float seconds) const noexcept {
    for (auto& body : bodies) {
        body.velocity = body.velocity + gravity_ * seconds;
        body.position = body.position + body.velocity * seconds;
        const float bottom = body.position.y + body.half_extent.y;
        body.grounded = bottom >= ground_y_;
        if (body.grounded) { body.position.y = ground_y_ - body.half_extent.y; body.velocity.y = std::min(0.0f, body.velocity.y); }
    }
}

float AnimationClip::sample(float time) const noexcept {
    if (keys_.empty()) return 0;
    if (time <= keys_.front().time) return keys_.front().value;
    if (time >= keys_.back().time) return keys_.back().value;
    const auto right = std::upper_bound(keys_.begin(), keys_.end(), time,
        [](float t, const Keyframe& key) { return t < key.time; });
    const auto& b = *right;
    const auto& a = *(right - 1);
    const float mix = (time - a.time) / (b.time - a.time);
    return a.value + (b.value - a.value) * mix;
}

bool InputState::pressed(Action action) const noexcept {
    switch (action) {
        case Action::Accept: return accept_pressed;
        case Action::Cancel: return cancel_pressed;
        case Action::Skip: return skip_pressed;
        case Action::Up: return up_pressed;
        case Action::Down: return down_pressed;
        case Action::Left: return left_pressed;
        case Action::Right: return right_pressed;
        case Action::Quit: return quit;
    }
    return false;
}

} // namespace sagas
