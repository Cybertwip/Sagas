#include <sagas/Engine.hpp>
#include <sagas/Fighter.hpp>
#include <sagas/FighterDescriptors.hpp>
#include <sagas/FighterSourceData.hpp>
#include <sagas/RemixDescriptors.hpp>
#include <numbers>
#include <sagas/Scene3D.hpp>
#include <sagas/SceneResources.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <iomanip>
#include <optional>
#include <string>
#include <string_view>
#include <sstream>
#include <unordered_set>

namespace sagas {
namespace {

constexpr std::array<FighterKind,12> kBuiltinKinds{
    FighterKind::Luigi, FighterKind::Mario, FighterKind::Donkey, FighterKind::Link,
    FighterKind::Samus, FighterKind::Captain, FighterKind::Ness, FighterKind::Yoshi,
    FighterKind::Kirby, FighterKind::Fox, FighterKind::Pikachu, FighterKind::Purin
};
constexpr std::array<float,12> kBuiltinX{
    25, 70, 115, 160, 205, 250, 25, 70, 115, 160, 205, 250
};
constexpr std::array<float,12> kBuiltinY{
    36, 36, 36, 36, 36, 36, 79, 79, 79, 79, 79, 79
};
// Smash Remix CharacterSelect.asm layout.slot_1 .. slot_30 (NUM_COLUMNS=10).
constexpr std::array<std::string_view,30> kRemixSlots{
    "MARINA","DRM","LUIGI","MARIO","DONKEY","LINK","SAMUS","CAPTAIN","GND","SONIC",
    "DEDEDE","YLINK","NESS","YOSHI","KIRBY","FOX","PIKACHU","JIGGLYPUFF","FALCO","SHEIK",
    "GOEMON","CRASH","WARIO","PEACH","BOWSER","WOLF","CONKER","MTWO","MARTH","BANJO"
};
constexpr int kCssColumns=10, kCssCell=24, kCssVisibleRows=3;
constexpr float kCssStartX=39.f, kCssStartY=44.f;

void custom_label(RenderEngine& render,std::string_view name,float x,float y,float width,float height,Color color={255,255,255,255}) {
    const float advance=std::min(height*.72f,width/std::max<std::size_t>(name.size(),1));
    for (unsigned char ch:name) {
        ch=static_cast<unsigned char>(std::toupper(ch));
        if (ch>='A' && ch<='Z') render.sprite_rect("textures/MNCommonFonts/Letter"+std::string(1,static_cast<char>(ch))+".png",x,y,advance,height,color);
        x+=advance;
    }
}

std::optional<FighterKind> builtin_kind(std::string_view key) {
    static constexpr std::array<std::pair<std::string_view,FighterKind>,14> keys{{
        {"LUIGI",FighterKind::Luigi},{"MARIO",FighterKind::Mario},{"DONKEY",FighterKind::Donkey},
        {"LINK",FighterKind::Link},{"SAMUS",FighterKind::Samus},{"CAPTAIN",FighterKind::Captain},
        {"NESS",FighterKind::Ness},{"YOSHI",FighterKind::Yoshi},{"KIRBY",FighterKind::Kirby},
        {"FOX",FighterKind::Fox},{"PIKACHU",FighterKind::Pikachu},{"PURIN",FighterKind::Purin},
        {"JIGGLYPUFF",FighterKind::Purin},{"JIGGLY",FighterKind::Purin}
    }};
    for (const auto& [name,kind]:keys) if (name==key) return kind;
    return {};
}

std::string_view card_name_file(FighterKind kind) {
    constexpr std::array<std::string_view,12> names{
        "LuigiText.png","MarioText.png","DKText.png","LinkText.png","SamusText.png",
        "CaptainFalconText.png","NessText.png","YoshiText.png","KirbyText.png","FoxText.png",
        "PikachuText.png","JigglypuffText.png"};
    const auto index=static_cast<unsigned>(kind);
    return index<names.size()?names[index]:names[1];
}

std::string_view series_emblem(FighterKind kind) {
    constexpr std::array<std::string_view,12> emblems{
        "Mario","Mario","Donkey","Zelda","Metroid","FZero",
        "Mother","Yoshi","Kirby","Fox","PMonsters","PMonsters"};
    const auto index=static_cast<unsigned>(kind);
    return index<emblems.size()?emblems[index]:emblems[1];
}

std::string css_portrait_for(std::string_view key) {
    std::string alt;
    for (const auto& entry:remix_css) {
        if (entry.key==key) return entry.portrait;
        if ((key=="PURIN" && entry.key=="JIGGLYPUFF") ||
            (key=="JIGGLYPUFF" && (entry.key=="PURIN" || entry.key=="JIGGLY")))
            alt=entry.portrait;
    }
    return alt;
}

enum class SlotKind { Human, Cpu, None };

class CharacterSelectScene final : public Scene {
public:
    CharacterSelectScene(int stock, bool team, bool one_player)
        : stock_(stock), team_(team), one_player_(one_player) {
        slots_[0] = {SlotKind::Human, FighterKind::Mario, false};
        for (int i = 1; i < 4; ++i)
            slots_[i] = {SlotKind::None, FighterKind::Mario, false};
        cursor_x_ = kBuiltinX[1] + 22;
        cursor_y_ = kBuiltinY[1] + 22;
        slots_[0].puck=constrained_puck(cursor_x_,cursor_y_);
    }
    void enter(Services& services) override {
        assets_=&services.assets;
        std::vector<FighterKind> kinds;
        std::vector<std::string> portraits,models,names;
        std::unordered_set<std::string> used;
        const auto push=[&](FighterKind kind,std::string portrait,std::string model,std::string name) {
            if (!portrait.empty() && (portrait.find("..")!=std::string::npos ||
                (portrait.find("mods/")!=0 && portrait.find("css/")!=0 && portrait.find("textures/")!=0) ||
                !services.assets.exists(portrait))) portrait.clear();
            if (!model.empty() && model.rfind("remix:",0)!=0 &&
                (model.find("mods/")!=0 || model.find("..")!=std::string::npos || !services.assets.exists(model)))
                model.clear();
            kinds.push_back(kind);
            portraits.push_back(std::move(portrait));
            models.push_back(std::move(model));
            names.push_back(std::move(name));
        };
        const auto add_remix=[&](const RemixFighter& fighter) {
            if (fighter.key=="RANDOM" || fighter.parent>=static_cast<unsigned>(FighterKind::Count)) return;
            if (!used.insert(fighter.key).second) return;
            std::ostringstream reloc;reloc<<"reloc/"<<std::setw(4)<<std::setfill('0')<<fighter.files[3]<<".bin";
            if (!services.assets.exists(reloc.str())) return;
            auto portrait=css_portrait_for(fighter.key);
            push(static_cast<FighterKind>(fighter.parent),std::move(portrait),"remix:"+fighter.key,fighter.key);
        };
        if (one_player_) {
            for (unsigned i=0;i<kBuiltinKinds.size();++i) {
                const auto kind=kBuiltinKinds[i];
                push(kind,std::string("textures/MNPlayersPortraits/")+std::string(fighter_portrait_file(kind)),
                     "",std::string(fighter_kind_name(kind)));
            }
        } else {
            for (const auto slot:kRemixSlots) {
                if (const auto kind=builtin_kind(slot)) {
                    used.insert(std::string(slot));
                    auto portrait=css_portrait_for(slot);
                    if (portrait.empty())
                        portrait=std::string("textures/MNPlayersPortraits/")+std::string(fighter_portrait_file(*kind));
                    push(*kind,std::move(portrait),"",std::string(slot));
                    continue;
                }
                const auto* fighter=remix_fighter(slot);
                if (fighter) add_remix(*fighter);
            }
            for (const auto& fighter:remix_roster) add_remix(fighter);
            if (services.assets.exists("mods/roster.tsv")) {
                const auto bytes=services.assets.blob("mods/roster.tsv");
                std::istringstream input(std::string(reinterpret_cast<const char*>(bytes->data()),bytes->size()));
                std::string line;std::getline(input,line);
                int seen=0;
                while (std::getline(input,line) && kinds.size()<120) {
                    std::istringstream fields(line);std::string base,name,portrait,model,cell;
                    if (!std::getline(fields,base,'\t') || !std::getline(fields,name,'\t')) continue;
                    std::getline(fields,portrait,'\t');std::getline(fields,model,'\t');std::getline(fields,cell,'\t');
                    ++seen;if (seen<=12) continue;
                    int index=-1;try {index=std::stoi(base);} catch (...) {continue;}
                    if (index<0 || index>=12) continue;
                    if (!used.insert(name).second) continue;
                    push(static_cast<FighterKind>(index),portrait,model,name);
                }
            }
        }
        kPortraitKind=std::move(kinds);custom_portraits_=std::move(portraits);custom_models_=std::move(models);custom_names_=std::move(names);
        css_scroll_=0;layout_pages();
        int mario=1;
        for (unsigned i=0;i<custom_names_.size();++i)
            if (custom_names_[i]=="MARIO" && (i>=custom_models_.size() || custom_models_[i].empty())) {mario=static_cast<int>(i);break;}
        if (static_cast<unsigned>(mario)<kPortraitX.size()) {
            cursor_x_=kPortraitX[mario]+cell_w()*0.5f;
            cursor_y_=kPortraitY[mario]+cell_h()*0.5f;
            slots_[0].entry=mario;slots_[0].fkind=kPortraitKind[mario];
            slots_[0].puck=constrained_puck(cursor_x_,cursor_y_);
        }
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
        if (!one_player_) {
            const int rows=static_cast<int>((kPortraitKind.size()+kCssColumns-1)/kCssColumns);
            const int max_scroll=std::max(0,rows-kCssVisibleRows);
            int next=css_scroll_;
            if (cursor_y_<=46.f && input.stick_y>28 && tic_%8==0) next=css_scroll_-1;
            if (cursor_y_>=110.f && input.stick_y<-28 && tic_%8==0) next=css_scroll_+1;
            next=std::clamp(next,0,max_scroll);
            if (next!=css_scroll_) {css_scroll_=next;layout_pages();}
        }
        if (held_slot_>=0 && portrait_at(cursor_x_,cursor_y_)>=0) slots_[held_slot_].puck=constrained_puck(cursor_x_,cursor_y_);
        const int hover = portrait_at(cursor_x_,cursor_y_);
        if (held_slot_>=0 && hover>=0) {
            auto& slot=slots_[held_slot_];
            if (slot.entry!=hover || hover!=hover_) {
                if (hover!=hover_) services.audio.play(AudioCue::MenuScroll);
                slot.entry=hover;slot.fkind=kPortraitKind[hover];slot.selected=false;
                load_preview(slot.fkind,held_slot_,false);
            }
        }
        hover_=hover;
        const auto place=[&] {
            if (held_slot_<0 || hover_<0) return;
            auto& slot=slots_[held_slot_];
            slot.entry=hover_;slot.fkind=kPortraitKind[hover_];slot.selected=true;
            load_preview(slot.fkind,held_slot_,true);
            selected_tick_[held_slot_]=tic_;
            services.audio.play_character_fgm(static_cast<unsigned>(slot.entry)<custom_models_.size()?custom_models_[slot.entry]:"",fighter_source_data[static_cast<unsigned>(slot.fkind)].announce);
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
                    if (held_slot_>=0 && hover>=0) {
                        slots_[slot].entry=hover;slots_[slot].fkind=kPortraitKind[hover];
                        load_preview(slots_[slot].fkind,slot,false);
                    }
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
                cursors_[player]={kCssStartX+24.f*(player+1),kCssStartY+12.f};slot.puck={cursors_[player].x-6,cursors_[player].y-6};
                const int discovered=portrait_at(cursors_[player].x,cursors_[player].y);
                if (discovered>=0) {
                    slot.entry=discovered;slot.fkind=kPortraitKind[discovered];
                    load_preview(slot.fkind,player,false);
                }
            }
            auto& cursor=cursors_[player];cursor.x=std::clamp(cursor.x+c.x/20,0.f,300.f);cursor.y=std::clamp(cursor.y-c.y/20,10.f,230.f);
            const int portrait=portrait_at(cursor.x,cursor.y);
            if (!slot.selected) {
                if (portrait>=0) slot.puck=constrained_puck(cursor.x,cursor.y);
                if (portrait>=0 && (slot.entry!=portrait || previews_[player].nodes.empty())) {
                    slot.entry=portrait;slot.fkind=kPortraitKind[portrait];load_preview(slot.fkind,player,false);
                }
            }
            if (c.cancel && slot.selected) {slot.selected=false;load_preview(slot.fkind,player,false);}
            if (c.attack) {
                if (!slot.selected && portrait>=0) {slot.selected=true;slot.entry=portrait;slot.fkind=kPortraitKind[portrait];load_preview(slot.fkind,player,true);services.audio.play_character_fgm(static_cast<unsigned>(slot.entry)<custom_models_.size()?custom_models_[slot.entry]:"",fighter_source_data[static_cast<unsigned>(slot.fkind)].announce);}
                else if (slot.selected && cursor.x>=slot.puck.x && cursor.x<slot.puck.x+26 && cursor.y>=slot.puck.y && cursor.y<slot.puck.y+24) {slot.selected=false;load_preview(slot.fkind,player,false);}
            }
            if (c.start && ready()) start_=true;
        }
        if (input.start_pressed && ready()) start_=true;
    }

    void draw(Services& services) override {
        auto& r = services.render;
        r.begin({0,0,0,255});
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

        r.scissor_game(10,34,300,90);
        for (unsigned portrait = 0; portrait < kPortraitKind.size(); ++portrait) {
            const Vec2 pos{kPortraitX[portrait],kPortraitY[portrait]};
            if (pos.y<34.f || pos.y>=124.f) continue;
            const float width=cell_w(),height=cell_h();
            const auto kind = kPortraitKind[portrait];
            const auto portrait_file=portrait<custom_portraits_.size() && !custom_portraits_[portrait].empty()?custom_portraits_[portrait]:std::string("textures/MNPlayersPortraits/")+std::string(fighter_portrait_file(kind));
            if (one_player_) r.sprite_rect("textures/MNPlayersPortraits/PortraitFireBg.png",pos.x,pos.y,width,height);
            r.sprite_rect(portrait_file,pos.x,pos.y,width,height);
        }
        r.reset_scissor();

        const int gates = one_player_ ? 1 : 4;
        static constexpr std::array<const char*,4> pucks{
            "textures/MNPlayersCommon/1PPuck.png", "textures/MNPlayersCommon/2PPuck.png",
            "textures/MNPlayersCommon/3PPuck.png", "textures/MNPlayersCommon/4PPuck.png"};
        for (int player = 0; player < gates; ++player) {
            const float x = static_cast<float>(player * 69 + 22);
            static constexpr std::array<const char*,4> cards{"RedCard.png","GrayCard.png","GrayCard.png","GrayCard.png"};
            const std::string card=slots_[player].kind==SlotKind::None?"GrayCard.png":cards[player];
            r.sprite_at("textures/MNPlayersCommon/"+card,{x,one_player_?131.f:126.f});
            const auto entry=slots_[player].entry;
            const bool remix=entry>=0 && static_cast<unsigned>(entry)<custom_models_.size() &&
                custom_models_[entry].rfind("remix:",0)==0;
            if (slots_[player].kind!=SlotKind::None) {
                r.sprite_at("textures/FTEmblemSprites/"+std::string(series_emblem(slots_[player].fkind))+".png",
                            {x+2,143},{1,1},{30,30,30,255});
                if (remix && entry>=0 && static_cast<unsigned>(entry)<custom_names_.size())
                    custom_label(r,custom_names_[entry],x+4,201,60,10);
                else
                    r.sprite_at("textures/MNPlayersCommon/"+std::string(card_name_file(slots_[player].fkind)),{x,201});
            }
        }

        renderer_->begin();
        r.clear_depth();
        Camera3D camera{{0,0,5000},{0,0,0},{0,1,0},30,100,20000};
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
        if (held_slot_>=0) r.sprite_at(pucks[held_slot_],slots_[held_slot_].puck);
        const bool portrait_band=cursor_y_>=36 && cursor_y_<=124.f;
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
    std::vector<FighterKind> kPortraitKind{kBuiltinKinds.begin(),kBuiltinKinds.end()};
    std::vector<float> kPortraitX{kBuiltinX.begin(),kBuiltinX.end()},kPortraitY{kBuiltinY.begin(),kBuiltinY.end()};
    std::vector<std::string> custom_portraits_,custom_models_,custom_names_;
    AssetRepository* assets_{};
    int css_scroll_{};
    [[nodiscard]] float cell_w() const { return one_player_?45.f:static_cast<float>(kCssCell); }
    [[nodiscard]] float cell_h() const { return one_player_?43.f:static_cast<float>(kCssCell); }
    void layout_pages() {
        kPortraitX.assign(kPortraitKind.size(),0);kPortraitY.assign(kPortraitKind.size(),0);
        if (one_player_) {
            for (unsigned i=0;i<kPortraitKind.size();++i) {
                if (i<12) {kPortraitX[i]=kBuiltinX[i];kPortraitY[i]=kBuiltinY[i];}
                else {
                    kPortraitX[i]=25.f+((i-12)%6)*45.f;
                    kPortraitY[i]=36.f+((i-12)/6)*43.f;
                }
            }
            return;
        }
        for (unsigned i=0;i<kPortraitKind.size();++i) {
            const int column=static_cast<int>(i%kCssColumns);
            const int row=static_cast<int>(i/kCssColumns);
            kPortraitX[i]=kCssStartX+column*kCssCell;
            kPortraitY[i]=kCssStartY+(row-css_scroll_)*kCssCell;
        }
    }
    Vec2 constrained_puck(float x,float y) const {
        const float top=36.f;
        const float bottom=one_player_?98.f:116.f;
        return {std::clamp(x-6,25.f,269.f),std::clamp(y-6,top,bottom)};
    }
    struct Slot { SlotKind kind; FighterKind fkind; bool selected; Vec2 puck{};int entry{1}; };
    [[nodiscard]] int portrait_at(float x, float y) const {
        const float width=cell_w(),height=cell_h();
        for (unsigned i = 0; i < kPortraitKind.size(); ++i) {
            if (kPortraitY[i]<34.f || kPortraitY[i]>=124.f) continue;
            if (x >= kPortraitX[i] && x < kPortraitX[i] + width &&
                y >= kPortraitY[i] && y < kPortraitY[i] + height)
                return static_cast<int>(i);
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
        const auto entry=slots_[player].entry;
        const auto remix=entry>=0 && static_cast<unsigned>(entry)<custom_models_.size() &&
            custom_models_[entry].rfind("remix:",0)==0?std::string_view(custom_models_[entry]).substr(6):std::string_view{};
        unsigned clip=selected?data.selected:data.idle;
        if (!remix.empty()) clip=remix_motion_clip(remix,kind,clip);
        auto preview=loader_->fighter_motion(kind,clip,selected?data.selected_flags:0,remix);
        if (entry>=0 && static_cast<unsigned>(entry)<custom_models_.size() && !custom_models_[entry].empty() && remix.empty()) {
            const auto bytes=assets_->blob(custom_models_[entry]);loader_->apply_custom_mesh(preview,*bytes);
        }
        previews_[player]=std::move(preview);
        selected_tick_[player]=tic_;
    }
    int stock_{3};
    bool team_{};
    bool one_player_{};
    float cursor_x_{70}, cursor_y_{50};
    int hover_{-1};
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
        std::vector<FighterKind> fighters;std::vector<int> ports;std::vector<std::string> models;
        for (unsigned i=0;i<slots_.size();++i) if (slots_[i].kind!=SlotKind::None && slots_[i].selected) {
            models.push_back(static_cast<unsigned>(slots_[i].entry)<custom_models_.size()?custom_models_[slots_[i].entry]:"");
            fighters.push_back(slots_[i].fkind);ports.push_back(slots_[i].kind==SlotKind::Human?static_cast<int>(i):-1);
        }
        if (one_player_) {fighters.push_back(FighterKind::Donkey);ports.push_back(-1);}
        return make_battle_scene(std::move(fighters),stock_,std::move(ports),std::move(models),team_);
    }
    return {};
}

} // namespace

std::unique_ptr<Scene> make_character_select_scene(int stock, bool team, bool one_player) {
    return std::make_unique<CharacterSelectScene>(stock, team, one_player);
}

} // namespace sagas
