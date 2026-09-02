#include <sagas/Engine.hpp>
#include <sagas/Fighter.hpp>
#include <sagas/Scene3D.hpp>
#include <sagas/SceneResources.hpp>

#include <array>
#include <cmath>
#include <string>

namespace sagas {
namespace {

const char* descriptor_for(FighterKind kind) {
    switch (kind) {
        case FighterKind::Luigi: return "llLuigiModelJointTreeDObjDesc";
        case FighterKind::Donkey: return "llDonkeyModelJointTreeDObjDesc";
        case FighterKind::Link: return "llLinkModelJointTreeDObjDesc";
        case FighterKind::Samus: return "llSamusModelJointTreeDObjDesc";
        case FighterKind::Captain: return "llCaptainModelJointTreeDObjDesc";
        case FighterKind::Ness: return "llNessModelJointTreeDObjDesc";
        case FighterKind::Yoshi: return "llYoshiModelJointTreeDObjDesc";
        case FighterKind::Kirby: return "llKirbyModelJointTreeDObjDesc";
        case FighterKind::Fox: return "llFoxModelJointTreeDObjDesc";
        case FighterKind::Pikachu: return "llPikachuModelJointTreeDObjDesc";
        case FighterKind::Purin: return "llPurinModelJointTreeDObjDesc";
        default: return "llMarioModelJointTreeDObjDesc";
    }
}

class BattleScene final : public Scene {
public:
    BattleScene(FighterKind p1, FighterKind p2, int stock)
        : stock_(stock) {
        bodies_[0].kind = p1;
        bodies_[0].attr = fighter_attributes(p1);
        bodies_[0].position = {-40, 0, 0};
        bodies_[0].lr = 1;
        bodies_[1].kind = p2;
        bodies_[1].attr = fighter_attributes(p2);
        bodies_[1].position = {40, 0, 0};
        bodies_[1].lr = -1;
        stocks_[0] = stocks_[1] = stock_;
    }
    void enter(Services& services) override {
        loader_ = std::make_unique<Scene3DLoader>(services.resources.archive());
        renderer_ = std::make_unique<Scene3DRenderer>(services.resources.archive());
        for (int i = 0; i < 2; ++i) {
            try {
                models_[i] = loader_->fighter_model(descriptor_for(bodies_[i].kind),
                                                    GeometryLayout::Direct);
                models_[i].scale = {1,1,1};
            } catch (const std::exception&) {}
        }
    }
    void update(Services&, const InputState& input, float) override {
        ++tic_;
        if (input.cancel_pressed) done_ = true;
        bodies_[0].stick_x = input.left_pressed ? -80 : (input.right_pressed ? 80 : 0);
        bodies_[0].stick_y = input.up_pressed ? 80 : (input.down_pressed ? -80 : 0);
        if (input.up_pressed && bodies_[0].grounded && bodies_[0].status != FighterStatus::KneeBend) {
            bodies_[0].status = FighterStatus::KneeBend;
            bodies_[0].jump_frames = 0;
        }
        FighterPhysics::tick(bodies_[0], 0.0f);

        // Dummy CP: walk toward P1 and jump if below.
        const float dx = bodies_[0].position.x - bodies_[1].position.x;
        bodies_[1].stick_x = dx > 8 ? 48 : (dx < -8 ? -48 : 0);
        bodies_[1].stick_y = 0;
        if (bodies_[1].grounded && std::abs(dx) < 30 && (tic_ % 90) == 0 &&
            bodies_[1].status != FighterStatus::KneeBend) {
            bodies_[1].status = FighterStatus::KneeBend;
            bodies_[1].jump_frames = 0;
        }
        FighterPhysics::tick(bodies_[1], 0.0f);

        for (auto& body : bodies_) {
            body.position.x = std::clamp(body.position.x, -90.0f, 90.0f);
            if (body.position.y < -80.0f) {
                body.position = {body.lr > 0 ? -40.0f : 40.0f, 40.0f, 0};
                body.vel_air = {};
                body.vel_ground = 0;
                body.grounded = false;
            }
        }
    }
    void draw(Services& services) override {
        auto& r = services.render;
        r.begin({40, 70, 120, 255});
        r.sprite("textures/StageBattlefieldBackground/0x26c88.png", {160, 90}, {1.2f, 1.2f});
        r.fill(40, 148, 240, 18, {90, 70, 50, 255});
        r.fill(40, 148, 240, 4, {180, 150, 90, 255});

        renderer_->begin();
        Camera3D camera{{0, 40, 220},{0, 20, 0},{0,1,0},40.0f,8,4096};
        auto lights = LightingSystem::opening_room();
        lights.key.intensity = 0.5f;
        for (int i = 0; i < 2; ++i) {
            if (models_[i].nodes.empty()) {
                const float x = 160.0f + bodies_[i].position.x * 2.4f;
                const float y = 148.0f - bodies_[i].position.y * 2.4f - 28.0f;
                r.sprite(std::string("textures/MNPlayersPortraits/") +
                         std::string(fighter_portrait_file(bodies_[i].kind)),
                         {x, y}, {0.8f, 0.8f});
                continue;
            }
            auto model = models_[i];
            model.position = {bodies_[i].position.x * 12.0f, bodies_[i].position.y * 12.0f, 0};
            model.scale = {bodies_[i].lr >= 0 ? 1.0f : -1.0f, 1, 1};
            renderer_->draw(r, model, camera, static_cast<float>(tic_ % 80),
                            {255,255,255,255}, lights);
        }
        renderer_->end(r);

        for (int i = 0; i < 2; ++i) {
            const float x = i == 0 ? 28.0f : 200.0f;
            r.fill(x, 188, 92, 42, {0,0,0,160});
            r.sprite_at(std::string("textures/MNPlayersPortraits/") +
                        std::string(fighter_portrait_file(bodies_[i].kind)),
                        {x + 4, 190}, {0.45f, 0.45f});
            const int percent = static_cast<int>(bodies_[i].damage);
            r.sprite_at("textures/IFCommonPlayerTags/" + std::string(i == 0 ? "1P.png" : "CP.png"),
                        {x + 48, 192});
            r.sprite_at("textures/IFCommonPlayerDamage/Digit" + std::to_string(percent / 10 % 10) + ".png",
                        {x + 44, 206});
            r.sprite_at("textures/IFCommonPlayerDamage/Digit" + std::to_string(percent % 10) + ".png",
                        {x + 56, 206});
            r.sprite_at("textures/IFCommonPlayerDamage/SymbolPercent.png", {x + 70, 210});
        }
        r.end();
    }
    std::unique_ptr<Scene> next() override { return done_ ? make_character_select_scene(stock_, false) : nullptr; }
private:
    int stock_{3};
    int tic_{};
    bool done_{};
    std::array<int,2> stocks_{};
    std::array<FighterBody,2> bodies_{};
    std::array<Model3D,2> models_{};
    std::unique_ptr<Scene3DLoader> loader_;
    std::unique_ptr<Scene3DRenderer> renderer_;
};

} // namespace

std::unique_ptr<Scene> make_battle_scene(FighterKind p1, FighterKind p2, int stock) {
    return std::make_unique<BattleScene>(p1, p2, stock);
}

} // namespace sagas
