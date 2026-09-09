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
        slots_[0].puck=constrained_puck(cursor_x_,cursor_y_);
    }
    void enter(Services& services) override {
        archive_=&services.resources.archive();
        loader_ = std::make_unique<Scene3DLoader>(*archive_);
        renderer_ = std::make_unique<Scene3DRenderer>(services.resources.archive());
        load_preview(slots_[0].fkind,0,false);
        if (services.assets.exists("audio/battle_select.sgpcm"))
            services.audio.play_music("audio/battle_select.sgpcm", 0.85f, true);
    }
    void update(Services& services, const InputState& input, float) override {
        ++tic_;
        for (unsigned i=0;i<slots_.size();++i)
            door_offset_[i]=std::clamp(door_offset_[i]+(slots_[i].kind==SlotKind::None?2.f:-2.f),0.f,41.f);
        cursor_x_ = std::clamp(cursor_x_ + input.stick_x / 20.0f, 0.0f, 280.0f);
        cursor_y_ = std::clamp(cursor_y_ - input.stick_y / 20.0f, 10.0f, 230.0f);
        if (input.pointer_moved || input.pointer_pressed || input.pointer_released) {
            cursor_x_=std::clamp(input.pointer_x,0.f,300.f);
            cursor_y_=std::clamp(input.pointer_y,10.f,230.f);
        }
        if (held_slot_>=0) slots_[held_slot_].puck=constrained_puck(cursor_x_,cursor_y_);
        const int hover = portrait_at(cursor_x_,cursor_y_);
        if (hover!=hover_) {
            hover_=hover;
            if (hover>=0 && held_slot_>=0) {
                slots_[active_slot_].fkind=kPortraitKind[hover];
                load_preview(slots_[active_slot_].fkind,active_slot_,false);
                services.audio.play(AudioCue::MenuScroll);
            }
        }
        const auto place=[&] {
            if (held_slot_<0 || hover_<0) return;
            auto& slot=slots_[held_slot_];
            slot.fkind=kPortraitKind[hover_];slot.selected=true;
            load_preview(slot.fkind,held_slot_,true);
            selected_tick_[held_slot_]=tic_;
            services.audio.play_fgm(fighter_source_data[static_cast<unsigned>(slot.fkind)].announce);
            held_slot_=-1;
        };
        if (input.accept_pressed || input.pointer_pressed) {
            int pickup=-1;
            if (held_slot_<0)
                for (unsigned i=0;i<slots_.size();++i) {
                    const auto& slot=slots_[i];
                    if (slot.selected && cursor_x_>=slot.puck.x && cursor_x_<=slot.puck.x+26 &&
                        cursor_y_>=slot.puck.y && cursor_y_<=slot.puck.y+24) {pickup=static_cast<int>(i);break;}
                }
            if (pickup>=0) {
                active_slot_=pickup;held_slot_=pickup;
                slots_[pickup].selected=false;
                slots_[pickup].puck=constrained_puck(cursor_x_,cursor_y_);
                load_preview(slots_[pickup].fkind,pickup,false);
                services.audio.play(AudioCue::MenuSelect);
            } else if ((one_player_?(cursor_y_>=195 && cursor_y_<214):(cursor_y_>=128 && cursor_y_<143)) && cursor_x_>=22) {
                const int slot=static_cast<int>((cursor_x_-22)/69);
                if (slot>=0 && slot<(one_player_?1:4)) {
                    if (held_slot_>=0 && held_slot_!=slot) slots_[held_slot_].selected=false;
                    active_slot_=slot;
                    if (slot>0) slots_[slot].kind=slots_[slot].kind==SlotKind::None?SlotKind::Cpu:SlotKind::None;
                    slots_[slot].selected=false;previews_[slot]={};
                    held_slot_=slots_[slot].kind==SlotKind::None?-1:slot;
                    hover_=-1;
                    services.audio.play(AudioCue::MenuSelect);
                }
            } else if (input.accept_pressed) place();
            if (cursor_y_>=16 && cursor_y_<30 && cursor_x_>=244) back_=true;
        }
        if (input.pointer_released) place();
        if (input.cancel_pressed) {
            if (slots_[active_slot_].selected) {
                slots_[active_slot_].selected=false;held_slot_=static_cast<int>(active_slot_);
                slots_[active_slot_].puck=constrained_puck(cursor_x_,cursor_y_);
                load_preview(slots_[active_slot_].fkind,active_slot_,false);
            } else if (active_slot_!=0) {active_slot_=0;held_slot_=slots_[0].selected?-1:0;}
            else back_=true;
        }
        if (!one_player_) for (unsigned player=1;player<4;++player) {
            const auto& c=input.controllers[player-1];
            if (!c.connected) continue;
            auto& slot=slots_[player];
            if (slot.kind!=SlotKind::Human) {
                slot.kind=SlotKind::Human;slot.selected=false;
                cursors_[player]={47+45.f*(player+1),58};slot.puck={cursors_[player].x-6,cursors_[player].y-6};
            }
            auto& cursor=cursors_[player];cursor.x=std::clamp(cursor.x+c.x/20,0.f,300.f);cursor.y=std::clamp(cursor.y-c.y/20,10.f,230.f);
            const int portrait=portrait_at(cursor.x,cursor.y);
            if (!slot.selected) {
                slot.puck=constrained_puck(cursor.x,cursor.y);
                if (portrait>=0 && (slot.fkind!=kPortraitKind[portrait] || previews_[player].nodes.empty())) {
                    slot.fkind=kPortraitKind[portrait];load_preview(slot.fkind,player,false);
                }
            }
            if (c.cancel && slot.selected) {slot.selected=false;load_preview(slot.fkind,player,false);}
            if (c.attack) {
                if (!slot.selected && portrait>=0) {slot.selected=true;slot.fkind=kPortraitKind[portrait];load_preview(slot.fkind,player,true);}
                else if (slot.selected && cursor.x>=slot.puck.x && cursor.x<slot.puck.x+26 && cursor.y>=slot.puck.y && cursor.y<slot.puck.y+24) {slot.selected=false;load_preview(slot.fkind,player,false);}
            }
            if (c.start && ready()) start_=true;
        }
        if (input.start_pressed && ready()) start_=true;
    }

    void draw(Services& services) override {
        auto& r = services.render;
        r.begin({0,0,0,255});
        // mnPlayers1PGameMakeWallpaper: 64x32 wrap, anchored at (10,10).
        r.scissor_game(10,10,300,220);
        for (float y=10;y<230;y+=32)
            for (float x=10;x<310;x+=64)
                r.sprite_at("textures/MNSelectCommon/StoneBackground.png",{x,y});
        r.reset_scissor();
        if (one_player_)
            r.sprite_at("textures/MNPlayers1PMode/1PlayerGameText.png", {24,18});
        else {
            r.sprite_at("textures/MNPlayersGameModes/FreeForAllText.png", {24,18}, {1,1},
                        team_ ? Color{180,180,180,255} : Color{227,172,4,255});
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
            static constexpr std::array<const char*,4> cards{"RedCard.png","GrayCard.png","GrayCard.png","GrayCard.png"};
            const std::string card=slots_[player].kind==SlotKind::None?"GrayCard.png":cards[player];
            r.sprite_at("textures/MNPlayersCommon/"+card,{x,one_player_?131.f:126.f});
            if (slots_[player].selected) {
                r.sprite_at(std::string("textures/CharacterNames/") +
                            std::string(fighter_kind_name(slots_[player].fkind)) + ".png",{x+8,146});
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

        for (int player=0;player<gates;++player) {
            const float x=22+player*69.f;
            if (!one_player_ && door_offset_[player]>0) {
                r.scissor_game(x,126,66,91);
                r.sprite_at("textures/MNPlayersCommon/SmashLogoCardLeft.png",{x-41+door_offset_[player],126});
                r.sprite_at("textures/MNPlayersCommon/SmashLogoCardRight.png",{x+66-door_offset_[player],126});
                r.reset_scissor();
            }
            const auto label=slots_[player].kind==SlotKind::None?"NALabel.png":slots_[player].kind==SlotKind::Cpu?"CPLabel.png":"HmnLabel.png";
            r.sprite_at("textures/MNPlayersCommon/"+std::string(label),{x+(one_player_?8.f:42.f),one_player_?201.f:131.f});
        }
        if (ready() && (tic_-selected_tick_[active_slot_])%40<30) {
            for (float x=0;x<320;x+=8)
                r.sprite_at("textures/MNPlayersCommon/ReadyBanner.png",{x,71},{1,1},{244,86,127,255});
            r.sprite_rect("textures/MNPlayersCommon/ReadyToFightText.png",50,71,224,17,{255,255,157,255});
        }
        r.sprite_at("textures/MNPlayersCommon/BackButton.png",{244,16});
        for (int player=0;player<gates;++player)
            if (slots_[player].selected) r.sprite_at(pucks[player],slots_[player].puck);
        // Pucks render beneath the hand in both held and placed states.
        if (held_slot_>=0) r.sprite_at(pucks[held_slot_],slots_[held_slot_].puck);
        const bool portrait_band=cursor_y_>=38 && cursor_y_<=124;
        const int hand=portrait_band?(held_slot_>=0?1:2):0;
        constexpr std::array<const char*,3> hands{"CursorHandPoint.png","CursorHandGrab.png","CursorHandHover.png"};
        constexpr std::array<Vec2,3> label_offset{{{7,15},{9,10},{9,15}}};
        const Vec2 hand_pos{cursor_x_-17,cursor_y_+8};
        r.sprite_at("textures/MNPlayersCommon/"+std::string(hands[hand]),hand_pos);
        r.sprite_at("textures/MNPlayersCommon/1PTextGradient.png",
                    {hand_pos.x+label_offset[hand].x,hand_pos.y+label_offset[hand].y},{1,1},{224,21,21,255});
        if (!one_player_) for (unsigned player=1;player<4;++player) if (slots_[player].kind==SlotKind::Human) {
            if (!slots_[player].selected) r.sprite_at(pucks[player],slots_[player].puck);
            const auto cursor=cursors_[player];const Vec2 pos{cursor.x-17,cursor.y+8};
            r.sprite_at("textures/MNPlayersCommon/"+std::string(slots_[player].selected?"CursorHandHover.png":"CursorHandGrab.png"),pos);
            r.sprite_at("textures/MNPlayersCommon/"+std::to_string(player+1)+"PTextGradient.png",{pos.x+9,pos.y+10});
        }
        r.end();
    }
    std::unique_ptr<Scene> next() override;
private:
    static Vec2 constrained_puck(float x,float y) {
        // The hand can visit settings and Back; the whole token stays on portraits.
        return {std::clamp(x-6,25.f,269.f),std::clamp(y-6,36.f,98.f)};
    }
    struct Slot { SlotKind kind; FighterKind fkind; bool selected; Vec2 puck{}; };
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
        const auto& data=fighter_source_data[static_cast<unsigned>(kind)];
        auto preview=loader_->fighter_motion(kind,selected?data.selected:data.idle,
                                             selected?data.selected_flags:0);
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
    std::array<Vec2,4> cursors_{};
    std::array<float,4> door_offset_{41,41,41,41};
    std::array<Model3D,4> previews_{};
    std::array<int,4> selected_tick_{};
    unsigned active_slot_{};
    int held_slot_{0};
    n64::RelocArchive* archive_{};
    std::unique_ptr<Scene3DLoader> loader_;
    std::unique_ptr<Scene3DRenderer> renderer_;
};

std::unique_ptr<Scene> CharacterSelectScene::next() {
    if (back_) return make_menu_scene();
    if (start_) {
        std::vector<FighterKind> fighters;std::vector<int> ports;
        for (unsigned i=0;i<slots_.size();++i) if (slots_[i].kind!=SlotKind::None && slots_[i].selected) {
            fighters.push_back(slots_[i].fkind);ports.push_back(slots_[i].kind==SlotKind::Human?static_cast<int>(i):-1);
        }
        if (one_player_) {fighters.push_back(FighterKind::Donkey);ports.push_back(-1);}
        return make_battle_scene(std::move(fighters),stock_,std::move(ports));
    }
    return {};
}

} // namespace

std::unique_ptr<Scene> make_character_select_scene(int stock, bool team, bool one_player) {
    return std::make_unique<CharacterSelectScene>(stock, team, one_player);
}

} // namespace sagas
