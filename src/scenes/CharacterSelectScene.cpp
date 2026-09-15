#include <sagas/Engine.hpp>
#include <sagas/Fighter.hpp>
#include <sagas/FighterSourceData.hpp>
#include <sagas/RemixDescriptors.hpp>
#include <sagas/CssRoster.hpp>
#include <sagas/SceneDescriptors.hpp>
#include <sagas/Scene3D.hpp>
#include <sagas/SceneResources.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <string>
#include <string_view>

namespace sagas {
namespace {

enum class SlotKind { Human, Cpu, None };

class CharacterSelectScene final : public Scene {
public:
    CharacterSelectScene(int stock, bool team, bool one_player)
        : stock_(stock), team_(team), one_player_(one_player) {
        slots_[0] = {SlotKind::Human, FighterKind::Mario, false};
        for (int i = 1; i < 4; ++i)
            slots_[i] = {SlotKind::None, FighterKind::Mario, false};
    }
    void enter(Services& services) override {
        assets_=&services.assets;
        layout_=CssLayout::load();
        roster_=CssRoster::build(services, one_player_);
        if (!one_player_) services.render.set_logical_size(static_cast<int>(layout_.logical_w),static_cast<int>(layout_.logical_h));
        css_scroll_=0;layout_pages();
        int mario=1;
        for (unsigned i=0;i<roster_.entries.size();++i)
            if (roster_.entries[i].name=="MARIO" && roster_.entries[i].model.empty()) {mario=static_cast<int>(i);break;}
        if (static_cast<unsigned>(mario)<portrait_x_.size()) {
            cursor_x_=portrait_x_[mario]+cell_w()*0.5f;
            cursor_y_=portrait_y_[mario]+cell_h()*0.5f;
            slots_[0].entry=mario;slots_[0].fkind=roster_.entries[mario].kind;
            slots_[0].puck=constrained_puck(cursor_x_,cursor_y_);
        }
        archive_=&services.resources.archive();
        loader_ = std::make_unique<Scene3DLoader>(*archive_);
        renderer_ = std::make_unique<Scene3DRenderer>(services.resources.archive());
        load_preview(slots_[0].fkind,0,false);
        if (services.assets.exists(ui_path("css.music")))
            services.audio.play_music(ui_path("css.music"), 0.85f, true);
    }
    void update(Services& services, const InputState& input, float) override {
        ++tic_;
        if (!one_player_) services.render.set_logical_size(static_cast<int>(layout_.logical_w),static_cast<int>(layout_.logical_h));
        const float max_x=one_player_?280.f:layout_.logical_w-20.f;
        const float max_y=one_player_?230.f:layout_.logical_h-10.f;
        for (unsigned i=0;i<slots_.size();++i)
            door_offset_[i]=std::clamp(door_offset_[i]+(slots_[i].kind==SlotKind::None?2.f:-2.f),0.f,41.f);
        cursor_x_ = std::clamp(cursor_x_ + input.stick_x / 20.0f, 0.0f, max_x);
        cursor_y_ = std::clamp(cursor_y_ - input.stick_y / 20.0f, 10.0f, max_y);
        if (input.pointer_moved || input.pointer_pressed || input.pointer_released) {
            cursor_x_=std::clamp(input.pointer_x,0.f,max_x);
            cursor_y_=std::clamp(input.pointer_y,10.f,max_y);
        }
        if (!one_player_) {
            const int rows=static_cast<int>((roster_.entries.size()+layout_.columns-1)/layout_.columns);
            const int max_scroll=std::max(0,rows-layout_.visible_rows);
            int next=css_scroll_;
            if (cursor_y_<=layout_.start_y+8.f && input.stick_y>28 && tic_%8==0) next=css_scroll_-1;
            if (cursor_y_>=layout_.start_y+layout_.cell*layout_.visible_rows-8.f && input.stick_y<-28 && tic_%8==0) next=css_scroll_+1;
            next=std::clamp(next,0,max_scroll);
            if (next!=css_scroll_) {css_scroll_=next;layout_pages();}
        }
        if (held_slot_>=0 && portrait_at(cursor_x_,cursor_y_)>=0) slots_[held_slot_].puck=constrained_puck(cursor_x_,cursor_y_);
        const int hover = portrait_at(cursor_x_,cursor_y_);
        if (held_slot_>=0 && hover>=0) {
            auto& slot=slots_[held_slot_];
            if (slot.entry!=hover || hover!=hover_) {
                if (hover!=hover_) services.audio.play(AudioCue::MenuScroll);
                slot.entry=hover;slot.fkind=kind_at(hover);slot.selected=false;
                load_preview(slot.fkind,held_slot_,false);
            }
        }
        hover_=hover;
        const auto place=[&] {
            if (held_slot_<0 || hover_<0) return;
            auto& slot=slots_[held_slot_];
            slot.entry=hover_;slot.fkind=kind_at(hover_);slot.selected=true;
            load_preview(slot.fkind,held_slot_,true);
            selected_tick_[held_slot_]=tic_;
            services.audio.play_character_fgm(model_at(slot.entry),fighter_source_data[static_cast<unsigned>(slot.fkind)].announce);
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
            } else if ((one_player_?(cursor_y_>=195 && cursor_y_<214):(cursor_y_>=layout_.card_y && cursor_y_<layout_.card_y+24)) &&
                       cursor_x_>=layout_.card_x(one_player_,0)) {
                const int slot=static_cast<int>((cursor_x_-layout_.card_x(one_player_,0))/(one_player_?layout_.card_1p_step:layout_.card_step));
                if (slot>=0 && slot<(one_player_?1:4)) {
                    if (held_slot_>=0 && held_slot_!=slot) slots_[held_slot_].selected=false;
                    active_slot_=slot;
                    if (slot>0) slots_[slot].kind=slots_[slot].kind==SlotKind::None?SlotKind::Cpu:SlotKind::None;
                    slots_[slot].selected=false;previews_[slot]={};
                    held_slot_=slots_[slot].kind==SlotKind::None?-1:slot;
                    hover_=-1;
                    if (held_slot_>=0 && hover>=0) {
                        slots_[slot].entry=hover;slots_[slot].fkind=kind_at(hover);
                        load_preview(slots_[slot].fkind,slot,false);
                    }
                    services.audio.play(AudioCue::MenuSelect);
                }
            } else if (input.accept_pressed) place();
            if (cursor_y_>=16 && cursor_y_<36 && cursor_x_>=(one_player_?244.f:540.f)) back_=true;
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
                cursors_[player]={layout_.start_x+layout_.cell*(player+1),layout_.start_y+layout_.cell*0.5f};slot.puck={cursors_[player].x-6,cursors_[player].y-6};
                const int discovered=portrait_at(cursors_[player].x,cursors_[player].y);
                if (discovered>=0) {
                    slot.entry=discovered;slot.fkind=kind_at(discovered);
                    load_preview(slot.fkind,player,false);
                }
            }
            auto& cursor=cursors_[player];cursor.x=std::clamp(cursor.x+c.x/20,0.f,layout_.logical_w-20.f);cursor.y=std::clamp(cursor.y-c.y/20,10.f,layout_.logical_h-10.f);
            const int portrait=portrait_at(cursor.x,cursor.y);
            if (!slot.selected) {
                if (portrait>=0) slot.puck=constrained_puck(cursor.x,cursor.y);
                if (portrait>=0 && (slot.entry!=portrait || previews_[player].nodes.empty())) {
                    slot.entry=portrait;slot.fkind=kind_at(portrait);load_preview(slot.fkind,player,false);
                }
            }
            if (c.cancel && slot.selected) {slot.selected=false;load_preview(slot.fkind,player,false);}
            if (c.attack) {
                if (!slot.selected && portrait>=0) {slot.selected=true;slot.entry=portrait;slot.fkind=kind_at(portrait);load_preview(slot.fkind,player,true);services.audio.play_character_fgm(model_at(slot.entry),fighter_source_data[static_cast<unsigned>(slot.fkind)].announce);}
                else if (slot.selected && cursor.x>=slot.puck.x && cursor.x<slot.puck.x+26 && cursor.y>=slot.puck.y && cursor.y<slot.puck.y+24) {slot.selected=false;load_preview(slot.fkind,player,false);}
            }
            if (c.start && ready()) start_=true;
        }
        if (input.start_pressed && ready()) start_=true;
    }

    void draw(Services& services) override {
        auto& r = services.render;
        r.begin({0,0,0,255});
        if (!one_player_) r.set_logical_size(static_cast<int>(layout_.logical_w),static_cast<int>(layout_.logical_h));
        const float screen_w=layout_.width(one_player_);
        const float screen_h=layout_.height(one_player_);
        r.scissor_game(10,10,screen_w-20,screen_h-20);
        for (float y=10;y<screen_h-10;y+=32)
            for (float x=10;x<screen_w-10;x+=64)
                r.sprite_at(ui_path("css.background"),{x,y});
        r.reset_scissor();
        if (one_player_)
            r.sprite_at(ui_path("css.title_1p"), {24,18});
        else {
            r.sprite_at(ui_path("css.title_ffa"), {24,16},{1.5f,1.5f},
                        team_ ? Color{180,180,180,255} : Color{227,172,4,255});
            if (team_) r.sprite_at(ui_path("css.title_team"), {220,16},{1.5f,1.5f});
        }

        const float band_top=one_player_?layout_.start_1p_y:layout_.start_y;
        const float band_bottom=one_player_?124.f:layout_.start_y+layout_.cell*layout_.visible_rows;
        r.scissor_game(10,band_top,screen_w-20,band_bottom-band_top);
        for (unsigned portrait = 0; portrait < roster_.entries.size(); ++portrait) {
            const Vec2 pos{portrait_x_[portrait],portrait_y_[portrait]};
            if (pos.y<band_top-1.f || pos.y>=band_bottom) continue;
            const float width=cell_w(),height=cell_h();
            const auto& entry = roster_.entries[portrait];
            const auto portrait_file=entry.portrait;
            const bool vanilla=portrait_file.find("MNPlayersPortraits/")!=std::string::npos;
            r.sprite_rect(ui_path("css.fire"),pos.x,pos.y,width,height);
            if (!portrait_file.empty()) {
                if (vanilla) r.sprite_rect(portrait_file,pos.x+1,pos.y+1,width-2,height-2);
                else r.sprite_rect(portrait_file,pos.x,pos.y,width,height);
            }
        }
        r.reset_scissor();

        const int gates = one_player_ ? 1 : 4;
        static constexpr std::array<const char*,4> puck_ids{"css.puck_1","css.puck_2","css.puck_3","css.puck_4"};
        for (int player = 0; player < gates; ++player) {
            const float x = layout_.card_x(one_player_,player);
            const float y = layout_.card_top(one_player_);
            const auto& card=slots_[player].kind==SlotKind::None || player!=0?ui_path("css.card_gray"):ui_path("css.card_red");
            if (one_player_) r.sprite_at(card,{x,y});
            else r.sprite_rect(card,x,y,layout_.card_w,layout_.card_h);
        }

        renderer_->begin();
        r.clear_depth();
        Camera3D camera{{0,0,layout_.camera_z},{0,0,0},{0,1,0},layout_.camera_fov,100,20000};
        camera.aspect=layout_.camera_aspect_w/layout_.camera_aspect_h;
        if (!one_player_) camera.viewport={10,10,layout_.logical_w-20,layout_.logical_h-20};
        const float vw=one_player_?300.f:layout_.logical_w-20;
        const float lw=layout_.width(one_player_), lh=layout_.height(one_player_);
        const float sx=vw/lw/(camera.aspect*(lw/lh));
        const float world_per_px=layout_.camera_z*std::tan((layout_.camera_fov*0.5f)*std::numbers::pi_v<float>/180.0f)/(sx*lw*0.5f);
        for (int player=0;player<gates;++player) if (!previews_[player].nodes.empty() && slots_[player].kind!=SlotKind::None) {
            auto model=previews_[player];
            const float select=fighter_source_data[static_cast<unsigned>(slots_[player].fkind)].select_scale;
            model.scale={select,select,select};
            if (one_player_) model.position={player*840.0f-1250,layout_.fighter_y,0};
            else {
                const float card_cx=layout_.card_x0+layout_.card_w*0.5f+player*layout_.card_step;
                model.position={(card_cx-layout_.logical_w*0.5f)*world_per_px,layout_.fighter_y,0};
            }
            model.rotation.y=slots_[player].selected?0.0f:tic_*std::numbers::pi_v<float>/90;
            renderer_->draw(r,model,camera,static_cast<float>(tic_-selected_tick_[player]));
        }
        renderer_->end(r);

        for (int player=0;player<gates;++player) {
            const float x=layout_.card_x(one_player_,player);
            if (one_player_ && door_offset_[player]>0) {
                r.scissor_game(x,126,66,91);
                r.sprite_at(ui_path("css.door_left"),{x-41+door_offset_[player],126});
                r.sprite_at(ui_path("css.door_right"),{x+66-door_offset_[player],126});
                r.reset_scissor();
            }
            const auto entry=slots_[player].entry;
            const bool remix=model_at(entry).rfind("remix:",0)==0;
            if (slots_[player].kind!=SlotKind::None) {
                const float name_y=one_player_?201.f:layout_.card_y+layout_.card_h-20;
                if (remix)
                    draw_name_label(r,name_at(entry),x+8,name_y,one_player_?60.f:layout_.card_w-16,one_player_?10.f:16.f);
                else
                    r.sprite_at(std::string(css_card_name(slots_[player].fkind)),
                                {x+(one_player_?0.f:8.f),name_y},{one_player_?1.f:1.5f,one_player_?1.f:1.5f});
            }
            const auto& label=slots_[player].kind==SlotKind::None?ui_path("css.label_na"):slots_[player].kind==SlotKind::Cpu?ui_path("css.label_cpu"):ui_path("css.label_hmn");
            r.sprite_at(label,
                        {x+(one_player_?8.f:layout_.card_w-48.f),one_player_?201.f:layout_.card_y+6},
                        {one_player_?1.f:1.4f,one_player_?1.f:1.4f});
        }
        if (ready() && (tic_-selected_tick_[active_slot_])%40<30) {
            const float banner_y=one_player_?71.f:layout_.start_y+layout_.cell;
            for (float x=0;x<screen_w;x+=8)
                r.sprite_at(ui_path("css.ready_banner"),{x,banner_y},{1,1},{244,86,127,255});
            r.sprite_rect(ui_path("css.ready_text"),screen_w*0.5f-112,banner_y,224,17,{255,255,157,255});
        }
        r.sprite_at(ui_path("css.back"),
                    {one_player_?244.f:screen_w-90.f,16},{one_player_?1.f:1.5f,one_player_?1.f:1.5f});
        const Vec2 puck_scale=one_player_?Vec2{1,1}:Vec2{1.6f,1.6f};
        for (int player=0;player<gates;++player)
            if (slots_[player].selected) r.sprite_at(ui_path(puck_ids[player]),slots_[player].puck,puck_scale);
        if (held_slot_>=0) r.sprite_at(ui_path(puck_ids[held_slot_]),slots_[held_slot_].puck,puck_scale);
        const bool portrait_band=cursor_y_>=band_top && cursor_y_<=band_bottom;
        const int hand=portrait_band?(held_slot_>=0?1:2):0;
        static constexpr std::array<const char*,3> hands{"css.hand_point","css.hand_grab","css.hand_hover"};
        static constexpr std::array<const char*,4> tags{"css.tag_1","css.tag_2","css.tag_3","css.tag_4"};
        constexpr std::array<Vec2,3> label_offset{{{7,15},{9,10},{9,15}}};
        const float hand_scale=one_player_?1.f:1.6f;
        const Vec2 hand_pos{cursor_x_-17*hand_scale,cursor_y_+8*hand_scale};
        r.sprite_at(ui_path(hands[hand]),hand_pos,{hand_scale,hand_scale});
        r.sprite_at(ui_path("css.tag_1"),
                    {hand_pos.x+label_offset[hand].x*hand_scale,hand_pos.y+label_offset[hand].y*hand_scale},
                    {hand_scale,hand_scale},{224,21,21,255});
        if (!one_player_) for (unsigned player=1;player<4;++player) if (slots_[player].kind==SlotKind::Human) {
            if (!slots_[player].selected) r.sprite_at(ui_path(puck_ids[player]),slots_[player].puck,puck_scale);
            const auto cursor=cursors_[player];const Vec2 pos{cursor.x-17*hand_scale,cursor.y+8*hand_scale};
            r.sprite_at(ui_path(slots_[player].selected?"css.hand_hover":"css.hand_grab"),
                        pos,{hand_scale,hand_scale});
            r.sprite_at(ui_path(tags[player]),
                        {pos.x+9*hand_scale,pos.y+10*hand_scale},{hand_scale,hand_scale});
        }
        r.end();
    }
    std::unique_ptr<Scene> next() override;
private:
    CssLayout layout_{};
    CssRoster roster_{};
    std::vector<float> portrait_x_, portrait_y_;
    AssetRepository* assets_{};
    int css_scroll_{};
    [[nodiscard]] float cell_w() const { return layout_.portrait_w(one_player_); }
    [[nodiscard]] float cell_h() const { return layout_.portrait_h(one_player_); }
    [[nodiscard]] FighterKind kind_at(int entry) const {
        return entry>=0 && static_cast<unsigned>(entry)<roster_.entries.size()?roster_.entries[entry].kind:FighterKind::Mario;
    }
    [[nodiscard]] const std::string& model_at(int entry) const {
        static const std::string empty;
        return entry>=0 && static_cast<unsigned>(entry)<roster_.entries.size()?roster_.entries[entry].model:empty;
    }
    [[nodiscard]] const std::string& name_at(int entry) const {
        static const std::string empty;
        return entry>=0 && static_cast<unsigned>(entry)<roster_.entries.size()?roster_.entries[entry].name:empty;
    }
    void layout_pages() {
        portrait_x_.assign(roster_.entries.size(),0);portrait_y_.assign(roster_.entries.size(),0);
        if (one_player_) {
            for (unsigned i=0;i<roster_.entries.size();++i) {
                const int column=static_cast<int>(i%layout_.columns_1p);
                const int row=static_cast<int>(i/layout_.columns_1p);
                portrait_x_[i]=layout_.start_1p_x+column*layout_.cell_1p_w;
                portrait_y_[i]=layout_.start_1p_y+row*layout_.cell_1p_h;
            }
            return;
        }
        for (unsigned i=0;i<roster_.entries.size();++i) {
            const int column=static_cast<int>(i%layout_.columns);
            const int row=static_cast<int>(i/layout_.columns);
            portrait_x_[i]=layout_.start_x+column*layout_.cell;
            portrait_y_[i]=layout_.start_y+(row-css_scroll_)*layout_.cell;
        }
    }
    Vec2 constrained_puck(float x,float y) const {
        const float top=one_player_?layout_.start_1p_y:layout_.start_y;
        const float bottom=one_player_?98.f:layout_.start_y+layout_.cell*layout_.visible_rows-8.f;
        const float left=one_player_?layout_.start_1p_x:layout_.start_x;
        const float right=one_player_?269.f:layout_.start_x+layout_.columns*layout_.cell-12.f;
        return {std::clamp(x-6,left,right),std::clamp(y-6,top,bottom)};
    }
    struct Slot { SlotKind kind; FighterKind fkind; bool selected; Vec2 puck{};int entry{1}; };
    [[nodiscard]] int portrait_at(float x, float y) const {
        const float width=cell_w(),height=cell_h();
        const float band_top=one_player_?34.f:layout_.start_y-1.f;
        const float band_bottom=one_player_?124.f:layout_.start_y+layout_.cell*layout_.visible_rows;
        for (unsigned i = 0; i < roster_.entries.size(); ++i) {
            if (portrait_y_[i]<band_top || portrait_y_[i]>=band_bottom) continue;
            if (x >= portrait_x_[i] && x < portrait_x_[i] + width &&
                y >= portrait_y_[i] && y < portrait_y_[i] + height)
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
        const auto& custom=model_at(entry);
        const auto remix=custom.rfind("remix:",0)==0?std::string_view(custom).substr(6):std::string_view{};
        unsigned clip=selected?data.selected:data.idle;
        if (!remix.empty()) clip=remix_motion_clip(remix,kind,clip);
        auto preview=loader_->fighter_motion(kind,clip,selected?data.selected_flags:0,remix);
        if (!custom.empty() && remix.empty()) {
            const auto bytes=assets_->blob(custom);loader_->apply_custom_mesh(preview,*bytes);
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
            models.push_back(model_at(slots_[i].entry));
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
