#include <sagas/Scene.hpp>

namespace sagas {

SceneMachine::SceneMachine(std::unique_ptr<Scene> initial, Services& services)
    : services_(services), scene_(std::move(initial)) { scene_->enter(services_); }
void SceneMachine::update(const InputState& input, float fixed_seconds) {
    scene_->update(services_, input, fixed_seconds);
    if (auto next = scene_->next()) { scene_ = std::move(next); scene_->enter(services_); }
}
void SceneMachine::draw() { scene_->draw(services_); }

} // namespace sagas
