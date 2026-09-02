#include <sagas/Engine.hpp>
#include <sagas/Fighter.hpp>

#include <array>
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

class CharacterSelectScene final : public Scene {
public:
    CharacterSelectScene(int stock, bool team)
        : stock_(stock), team_(team) {
        slots_[0] = {SlotKind::Human, FighterKind::Mario, false};
        for (int i = 1; i < 4; ++i) slots_[i] = {SlotKind::None, FighterKind::Mario, false};
    }
    void update(Services& services, const InputState& input, float) override {
        ++tic_;
        if (input.left_pressed) { cursor_ = (cursor_ + 11) % 12; services.audio.play(AudioCue::MenuScroll); }
        if (input.right_pressed) { cursor_ = (cursor_ + 1) % 12; services.audio.play(AudioCue::MenuScroll); }
        if (input.up_pressed) { cursor_ = (cursor_ + 6) % 12; services.audio.play(AudioCue::MenuScroll); }
        if (input.down_pressed) { cursor_ = (cursor_ + 6) % 12; services.audio.play(AudioCue::MenuScroll); }
        if (input.accept_pressed) {
            auto& slot = slots_[active_];
            slot.kind = SlotKind::Human;
            slot.fkind = kPortraitKind[static_cast<std::size_t>(cursor_)];
            slot.selected = true;
            services.audio.play(AudioCue::MenuSelect);
        }
        if (input.cancel_pressed) {
            if (slots_[active_].selected) slots_[active_].selected = false;
            else back_ = true;
        }
        if (input.skip_pressed && ready()) start_ = true;
        // Enter/Start on a selected P1 also proceeds, matching CSS "Press Start".
        if (input.accept_pressed && ready() && slots_[0].selected && held_ready_) start_ = true;
        held_ready_ = ready();
    }
    void draw(Services& services) override {
        auto& r = services.render;
        r.begin({0,0,0,255});
        r.sprite_at("textures/MNSelectCommon/StoneBackground.png", {10,10});
        r.sprite_at("textures/MNPlayersGameModes/FreeForAllText.png", {24,18}, {1,1},
                    team_ ? Color{180,180,180,255} : Color{255,255,255,255});
        if (team_) r.sprite_at("textures/MNPlayersGameModes/TeamBattleText.png", {140,18});

        for (int portrait = 0; portrait < 12; ++portrait) {
            const auto kind = kPortraitKind[static_cast<std::size_t>(portrait)];
            const Vec2 pos{kPortraitX[static_cast<std::size_t>(portrait)],
                           kPortraitY[static_cast<std::size_t>(portrait)]};
            r.sprite_at("textures/MNPlayersPortraits/PortraitFireBg.png", pos);
            r.sprite_at(std::string("textures/MNPlayersPortraits/") + std::string(fighter_portrait_file(kind)), pos);
        }
        const Vec2 cursor_pos{kPortraitX[static_cast<std::size_t>(cursor_)] + 8,
                              kPortraitY[static_cast<std::size_t>(cursor_)] + 20};
        r.sprite("textures/MNPlayersCommon/CursorHandPoint.png", cursor_pos);

        static constexpr std::array<const char*,4> pucks{"1PPuck.png","2PPuck.png","3PPuck.png","4PPuck.png"};
        static constexpr std::array<Color,4> colors{{{255,50,50,255},{50,80,255,255},{255,220,40,255},{40,200,70,255}}};
        for (int player = 0; player < 4; ++player) {
            const float x = static_cast<float>(player * 69 + 22);
            r.sprite_at("textures/MNPlayersCommon/GrayCard.png", {x, 131});
            if (slots_[player].kind == SlotKind::None) {
                r.sprite_at("textures/MNPlayersCommon/NALabel.png", {x + 12, 201});
            } else {
                r.sprite_at(slots_[player].kind == SlotKind::Cpu
                            ? "textures/MNPlayersCommon/CPLabel.png"
                            : "textures/MNPlayersCommon/HmnLabel.png", {x + 8, 201});
                if (slots_[player].selected) {
                    r.sprite_at(std::string("textures/MNPlayersPortraits/") +
                                std::string(fighter_portrait_file(slots_[player].fkind)),
                                {x + 6, 143}, {0.7f, 0.7f});
                    r.sprite_at(pucks[static_cast<std::size_t>(player)], {x + 18, 131}, {1,1},
                                colors[static_cast<std::size_t>(player)]);
                }
            }
        }
        if (ready()) {
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
    [[nodiscard]] bool ready() const {
        return slots_[0].selected && slots_[0].kind != SlotKind::None;
    }
    int stock_{3};
    bool team_{};
    int cursor_{1}; // Mario, matching the original default highlight
    int active_{};
    int tic_{};
    bool back_{};
    bool start_{};
    bool held_ready_{};
    std::array<Slot,4> slots_{};
};

} // namespace

std::unique_ptr<Scene> make_character_select_scene(int stock, bool team) {
    return std::make_unique<CharacterSelectScene>(stock, team);
}

} // namespace sagas
