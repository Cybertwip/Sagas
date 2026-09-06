#include <sagas/Engine.hpp>
#include <sagas/Fighter.hpp>
#include <sagas/FighterSourceData.hpp>
#include <numbers>
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

class CharacterSelectScene final : public Scene {
public:
    CharacterSelectScene(int stock, bool team, bool one_player)
        : stock_(stock), team_(team), one_player_(one_player) {
        slots_[0] = {SlotKind::Human, FighterKind::Mario, false};
        for (int i = 1; i < 4; ++i)
            slots_[i] = {SlotKind::None, FighterKind::Mario, false};
        cursor_x_ = kPortraitX[1] + 22;
        cursor_y_ = kPortraitY[1] + 22;
    }
    void enter(Services& services) override {
        archive_=&services.resources.archive();
        loader_ = std::make_unique<Scene3DLoader>(*archive_);
        renderer_ = std::make_unique<Scene3DRenderer>(services.resources.archive());
        load_preview(slots_[0].fkind,0,false);
        if (services.assets.exists("audio/battle_select.sgpcm"))
            services.audio.play_music("audio/battle_select.sgpcm", 0.85f);
    }
    void update(Services& services, const InputState& input, float) override {
        ++tic_;
        cursor_x_ = std::clamp(cursor_x_ + input.stick_x / 20.0f, 0.0f, 280.0f);
        cursor_y_ = std::clamp(cursor_y_ - input.stick_y / 20.0f, 10.0f, 205.0f);
        const int hover = portrait_at(cursor_x_,cursor_y_);
        if (hover!=hover_) {
            hover_=hover;
            if (hover>=0 && !slots_[active_slot_].selected) {
                slots_[active_slot_].fkind=kPortraitKind[hover];
                load_preview(slots_[active_slot_].fkind,active_slot_,false);
                services.audio.play(AudioCue::MenuScroll);
            }
        }
        if (input.accept_pressed) {
            if (cursor_y_>=195 && cursor_y_<214) {
                const int slot=static_cast<int>((cursor_x_-22)/69);
                if (slot>=0 && slot<(one_player_?1:4)) {
                    active_slot_=slot;
                    if (slot>0) slots_[slot].kind=slots_[slot].kind==SlotKind::None?SlotKind::Cpu:SlotKind::None;
                    slots_[slot].selected=false;
                    previews_[slot]={};
                    services.audio.play(AudioCue::MenuSelect);
                }
            } else if (hover_>=0 && slots_[active_slot_].kind!=SlotKind::None) {
                auto& slot=slots_[active_slot_];
                slot.fkind=kPortraitKind[hover_];slot.selected=true;
                load_preview(slot.fkind,active_slot_,true);
                selected_tick_[active_slot_]=tic_;
                services.audio.play_fgm(fighter_source_data[static_cast<unsigned>(slot.fkind)].announce);
            } else if (cursor_y_>=214 && cursor_x_<50) back_=true;
        }
        if (input.cancel_pressed) {
            if (slots_[active_slot_].selected) {
                slots_[active_slot_].selected=false;
                load_preview(slots_[active_slot_].fkind,active_slot_,false);
            } else if (active_slot_!=0) active_slot_=0;
            else back_=true;
        }
        if ((input.start_pressed || input.skip_pressed) && ready()) start_=true;
    }

    void draw(Services& services) override {
        auto& r = services.render;
        r.begin({0,0,0,255});
        r.sprite_rect("textures/MNSelectCommon/StoneBackground.png",0,0,320,240);
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
            static constexpr std::array<const char*,4> cards{"RedCard.png","BlueCard.png","YellowCard.png","GreenCard.png"};
            const std::string card=slots_[player].kind==SlotKind::None?"GrayCard.png":cards[player];
            r.sprite_at("textures/MNPlayersCommon/"+card,{x,131});
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
                    const auto portrait=std::find(kPortraitKind.begin(),kPortraitKind.end(),slots_[player].fkind)-kPortraitKind.begin();
                    r.sprite_at(pucks[player],{kPortraitX[portrait]+12,kPortraitY[portrait]+14});
                }
            }
        }

        renderer_->begin();
        r.clear_depth();
        Camera3D camera{{0,0,5000},{0,0,0},{0,1,0},30,100,20000};
        // This is a UI camera: preserve its source layout across the wide
        // canvas, so each preview stays inside its player's card.
        camera.aspect=45.0f/44.0f;
        for (int player=0;player<gates;++player) if (!previews_[player].nodes.empty() && slots_[player].kind!=SlotKind::None) {
            auto model=previews_[player];
            const float scale=fighter_source_data[static_cast<unsigned>(slots_[player].fkind)].select_scale;
            model.scale={scale,scale,scale};
            model.position={player*840.0f-1250,-850,0};
            model.rotation.y=slots_[player].selected?0.0f:tic_*std::numbers::pi_v<float>/90;
            renderer_->draw(r,model,camera,static_cast<float>(tic_-selected_tick_[player]));
        }
        renderer_->end(r);

        r.sprite("textures/MNPlayersCommon/CursorHandPoint.png", {cursor_x_ + 12, cursor_y_ + 8});
        if (!slots_[active_slot_].selected && slots_[active_slot_].kind!=SlotKind::None)
            r.sprite_at(pucks[active_slot_],{cursor_x_-6,cursor_y_-6});
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
    bool ready() const {
        unsigned count=0;
        for (const auto& slot:slots_) if (slot.kind!=SlotKind::None) {
            if (!slot.selected) return false;
            ++count;
        }
        return slots_[0].selected && count>=(one_player_?1U:2U);
    }
    void load_preview(FighterKind kind,unsigned player,bool selected) {
        if (!loader_) return;
        const auto spec=fighter_model_spec(kind);
        auto preview=loader_->fighter_model(spec.descriptor,spec.joint_pairs?GeometryLayout::JointPairs:GeometryLayout::Direct,spec.setup_parts);
        const auto& data=fighter_source_data[static_cast<unsigned>(kind)];
        preview.animation=n64::AnimationDecoder(*archive_).table({selected?data.selected:data.idle,0},preview.nodes.size());
        preview.fighter_animation=true;
        previews_[player]=std::move(preview);
        selected_tick_[player]=tic_;
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
    std::array<Model3D,4> previews_{};
    std::array<int,4> selected_tick_{};
    unsigned active_slot_{};
    n64::RelocArchive* archive_{};
    std::unique_ptr<Scene3DLoader> loader_;
    std::unique_ptr<Scene3DRenderer> renderer_;
};

std::unique_ptr<Scene> CharacterSelectScene::next() {
    if (back_) return make_menu_scene();
    if (start_) {
        std::vector<FighterKind> fighters;
        for (const auto& slot:slots_) if (slot.kind!=SlotKind::None && slot.selected) fighters.push_back(slot.fkind);
        if (one_player_) fighters.push_back(FighterKind::Donkey);
        return make_battle_scene(std::move(fighters),stock_);
    }
    return {};
}

} // namespace

std::unique_ptr<Scene> make_character_select_scene(int stock, bool team, bool one_player) {
    return std::make_unique<CharacterSelectScene>(stock, team, one_player);
}

} // namespace sagas
