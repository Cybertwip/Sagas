#include <sagas/Engine.hpp>
#include <sagas/Fighter.hpp>
#include <sagas/Scene3D.hpp>
#include <sagas/SceneResources.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <string>

namespace sagas {
namespace {

constexpr std::array<FighterKind,12> kPortraitKind{
    FighterKind::Luigi, FighterKind::Mario, FighterKind::Donkey, FighterKind::Link,
    FighterKind::Samus, FighterKind::Captain, FighterKind::Ness, FighterKind::Yoshi,
    FighterKind::Kirby, FighterKind::Fox, FighterKind::Pikachu, FighterKind::Purin
};
constexpr std::array<float,12> kPortraitX{
    25, 70, 115, 160, 205, 250, 25, 70, 115, 160, 205, 250
};
constexpr std::array<float,12> kPortraitY{
    36, 36, 36, 36, 36, 36, 79, 79, 79, 79, 79, 79
};

enum class SlotKind { Human, Cpu, None };

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

class CharacterSelectScene final : public Scene {
public:
    CharacterSelectScene(int stock, bool team, bool one_player)
        : stock_(stock), team_(team), one_player_(one_player) {
        slots_[0] = {SlotKind::Human, FighterKind::Mario, false};
        for (int i = 1; i < 4; ++i)
            slots_[i] = {one_player_ ? SlotKind::None : SlotKind::None, FighterKind::Mario, false};
        cursor_x_ = kPortraitX[1] + 22;
        cursor_y_ = kPortraitY[1] + 22;
    }
    void enter(Services& services) override {
        loader_ = std::make_unique<Scene3DLoader>(services.resources.archive());
        renderer_ = std::make_unique<Scene3DRenderer>(services.resources.archive());
        if (services.assets.exists("audio/battle_select.sgpcm"))
            services.audio.play_music("audio/battle_select.sgpcm", 0.85f);
    }
    void update(Services& services, const InputState& input, float) override {
        ++tic_;
        cursor_x_ = std::clamp(cursor_x_ + input.stick_x / 20.0f, 0.0f, 280.0f);
        cursor_y_ = std::clamp(cursor_y_ - input.stick_y / 20.0f, 10.0f, 205.0f);
        const int hover = portrait_at(cursor_x_, cursor_y_);
        if (hover >= 0 && hover != hover_) {
            hover_ = hover;
            services.audio.play(AudioCue::MenuScroll);
        } else if (hover >= 0) hover_ = hover;

        if (input.accept_pressed && hover_ >= 0) {
            auto& slot = slots_[0];
            slot.kind = SlotKind::Human;
            slot.fkind = kPortraitKind[static_cast<std::size_t>(hover_)];
            slot.selected = true;
            load_preview(slot.fkind);
            services.audio.play(AudioCue::MenuSelect);
        }
        if (input.cancel_pressed) {
            if (slots_[0].selected) { slots_[0].selected = false; preview_ = {}; }
            else back_ = true;
        }
        if ((input.start_pressed || input.skip_pressed) && slots_[0].selected) start_ = true;
    }
    void draw(Services& services) override {
        auto& r = services.render;
        r.begin({0,0,0,255});
        r.sprite_at("textures/MNSelectCommon/StoneBackground.png", {10,10});
        if (one_player_)
            r.sprite_at("textures/MNPlayers1PMode/1PlayerGameText.png", {24,18});
        else {
            r.sprite_at("textures/MNPlayersGameModes/FreeForAllText.png", {24,18}, {1,1},
                        team_ ? Color{180,180,180,255} : Color{255,255,255,255});
            if (team_) r.sprite_at("textures/MNPlayersGameModes/TeamBattleText.png", {140,18});
        }

        for (int portrait = 0; portrait < 12; ++portrait) {
            const auto kind = kPortraitKind[static_cast<std::size_t>(portrait)];
            const Vec2 pos{kPortraitX[static_cast<std::size_t>(portrait)],
                           kPortraitY[static_cast<std::size_t>(portrait)]};
            r.sprite_at("textures/MNPlayersPortraits/PortraitFireBg.png", pos);
            r.sprite_at(std::string("textures/MNPlayersPortraits/") + std::string(fighter_portrait_file(kind)), pos);
        }

        const int gates = one_player_ ? 1 : 4;
        static constexpr std::array<const char*,4> pucks{
            "textures/MNPlayersCommon/1PPuck.png", "textures/MNPlayersCommon/2PPuck.png",
            "textures/MNPlayersCommon/3PPuck.png", "textures/MNPlayersCommon/4PPuck.png"};
        for (int player = 0; player < gates; ++player) {
            const float x = static_cast<float>(player * 69 + 22);
            r.sprite_at(one_player_ ? "textures/MNPlayers1PMode/RedCard.png"
                                    : "textures/MNPlayersCommon/GrayCard.png", {x, 131});
            if (slots_[player].kind == SlotKind::None)
                r.sprite_at("textures/MNPlayersCommon/NALabel.png", {x + 12, 201});
            else {
                r.sprite_at(slots_[player].kind == SlotKind::Cpu
                            ? "textures/MNPlayersCommon/CPLabel.png"
                            : "textures/MNPlayersCommon/HmnLabel.png", {x + 8, 201});
                if (slots_[player].selected) {
                    r.sprite_at(std::string("textures/CharacterNames/") +
                                std::string(fighter_kind_name(slots_[player].fkind)) + ".png",
                                {x + 8, 134});
                    r.sprite_at(pucks[static_cast<std::size_t>(player)], {x + 22, 168});
                }
            }
        }

        if (slots_[0].selected && !preview_.nodes.empty()) {
            renderer_->begin();
            Camera3D camera{{0, 80, 280},{0, 40, 0},{0,1,0},30.0f,8,4096};
            auto model = preview_;
            model.position = {-70, -30, 0};
            renderer_->draw(r, model, camera, static_cast<float>(tic_ % 120),
                            {255,255,255,255}, LightingSystem::opening_room());
            renderer_->end(r);
        }

        r.sprite("textures/MNPlayersCommon/CursorHandPoint.png", {cursor_x_ + 12, cursor_y_ + 8});
        if (slots_[0].selected) {
            const auto pulse = static_cast<std::uint8_t>(180 + 75 * ((tic_ / 8) % 2));
            r.sprite("textures/MNPlayersCommon/ReadyToFightText.png", {160, 122}, {1,1},
                     {255,255,255,pulse});
            r.sprite("textures/MNPlayersCommon/PressText.png", {118, 14});
            r.sprite("textures/MNPlayersCommon/StartText.png", {168, 14});
        }
        r.sprite_at("textures/MNPlayersCommon/BackButton.png", {12, 214});
        r.end();
    }
    std::unique_ptr<Scene> next() override;
private:
    struct Slot { SlotKind kind; FighterKind fkind; bool selected; };
    [[nodiscard]] static int portrait_at(float x, float y) {
        for (int i = 0; i < 12; ++i) {
            if (x >= kPortraitX[static_cast<std::size_t>(i)] &&
                x < kPortraitX[static_cast<std::size_t>(i)] + 45 &&
                y >= kPortraitY[static_cast<std::size_t>(i)] &&
                y < kPortraitY[static_cast<std::size_t>(i)] + 43)
                return i;
        }
        return -1;
    }
    void load_preview(FighterKind kind) {
        if (!loader_) return;
        try { preview_ = loader_->fighter_model(descriptor_for(kind), GeometryLayout::Direct); }
        catch (const std::exception&) { preview_ = {}; }
    }
    int stock_{3};
    bool team_{};
    bool one_player_{};
    float cursor_x_{70}, cursor_y_{50};
    int hover_{1};
    int tic_{};
    bool back_{};
    bool start_{};
    std::array<Slot,4> slots_{};
    Model3D preview_{};
    std::unique_ptr<Scene3DLoader> loader_;
    std::unique_ptr<Scene3DRenderer> renderer_;
};

std::unique_ptr<Scene> CharacterSelectScene::next() {
    if (back_) return make_menu_scene();
    if (start_) {
        const auto p1 = slots_[0].fkind;
        const auto p2 = one_player_ ? FighterKind::Donkey : FighterKind::Fox;
        return make_battle_scene(p1, p2, stock_);
    }
    return {};
}

} // namespace

std::unique_ptr<Scene> make_character_select_scene(int stock, bool team, bool one_player) {
    return std::make_unique<CharacterSelectScene>(stock, team, one_player);
}

} // namespace sagas
