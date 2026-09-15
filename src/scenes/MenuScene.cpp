#include <sagas/Engine.hpp>
#include <sagas/SceneDescriptors.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <string>
#include <string_view>

namespace sagas {
namespace {

enum class MenuScreen {
    Main, OnePlayer, Versus, Options, Data,
    VSOptions, ItemSwitch, ScreenAdjust, SoundTest, VSRecord, BackupClear, Characters
};

std::string_view screen_id(MenuScreen screen) {
    switch (screen) {
        case MenuScreen::Main: return "main";
        case MenuScreen::OnePlayer: return "one_player";
        case MenuScreen::Versus: return "versus";
        case MenuScreen::Options: return "options";
        case MenuScreen::Data: return "data";
        case MenuScreen::VSOptions: return "vs_options";
        case MenuScreen::ItemSwitch: return "item_switch";
        case MenuScreen::ScreenAdjust: return "screen_adjust";
        case MenuScreen::SoundTest: return "sound_test";
        case MenuScreen::VSRecord: return "vs_record";
        case MenuScreen::BackupClear: return "backup_clear";
        case MenuScreen::Characters: return "characters";
    }
    return "main";
}

class MenuScene final : public Scene {
public:
    void enter(Services& services) override { services.audio.stop(); }
    void update(Services& services, const InputState& input, float) override {
        ++tic_;
        const auto count = item_count();
        const bool previous = input.up_pressed || input.right_pressed;
        const bool next = input.down_pressed || input.left_pressed;
        if (screen_==MenuScreen::ScreenAdjust) {
            adjust_x_ = std::clamp(adjust_x_ + input.stick_x / 40.0f, -20.f, 20.f);
            adjust_y_ = std::clamp(adjust_y_ - input.stick_y / 40.0f, -20.f, 20.f);
        } else if (previous != next) {
            cursor_ = previous ? (cursor_ + count - 1) % count : (cursor_ + 1) % count;
            services.audio.play(AudioCue::MenuScroll);
        }
        if (input.cancel_pressed) {
            if (screen_ == MenuScreen::Main) exit_to_title_ = true;
            else if (screen_ == MenuScreen::ItemSwitch) { screen_ = MenuScreen::VSOptions; cursor_ = 4; }
            else if (screen_ == MenuScreen::VSOptions) { screen_ = MenuScreen::Versus; cursor_ = 3; }
            else if (screen_ == MenuScreen::ScreenAdjust) { screen_ = MenuScreen::Options; cursor_ = 1; }
            else if (screen_ == MenuScreen::BackupClear) { screen_ = MenuScreen::Options; cursor_ = 2; }
            else if (screen_ == MenuScreen::Characters) { screen_ = MenuScreen::Data; cursor_ = 0; }
            else if (screen_ == MenuScreen::VSRecord) { screen_ = MenuScreen::Data; cursor_ = 1; }
            else if (screen_ == MenuScreen::SoundTest) { screen_ = MenuScreen::Data; cursor_ = 2; }
            else { screen_ = MenuScreen::Main; cursor_ = 0; }
        }
        if (screen_ == MenuScreen::Versus && cursor_ == 1 && (input.left_pressed || input.right_pressed)) {
            rule_ = (rule_ + (input.right_pressed ? 1 : 2)) % 3;
            team_battle_ = rule_ == 2;
            services.audio.play(AudioCue::MenuScroll);
        }
        if (screen_ == MenuScreen::Versus && cursor_ == 2 && (input.left_pressed || input.right_pressed)) {
            if (rule_==0) time_ = std::clamp(time_ + (input.right_pressed ? 1 : -1), 1, 99);
            else stock_ = std::clamp(stock_ + (input.right_pressed ? 1 : -1), 1, 99);
        }
        if (screen_ == MenuScreen::VSOptions && (input.left_pressed || input.right_pressed)) {
            const int dir = input.right_pressed ? 1 : -1;
            if (cursor_==0) handicap_ = std::clamp(handicap_ + dir, 0, 2);
            else if (cursor_==1) team_attack_ = !team_attack_;
            else if (cursor_==2) stage_select_ = !stage_select_;
            else if (cursor_==3) damage_ = std::clamp(damage_ + dir * (input.start_pressed ? 10 : 1), 0, 999);
            services.audio.play(AudioCue::MenuScroll);
        }
        if (screen_ == MenuScreen::ItemSwitch && (input.left_pressed || input.right_pressed)) {
            if (cursor_==0) item_rate_ = std::clamp(item_rate_ + (input.right_pressed?1:-1), 0, 5);
            else item_on_[cursor_] = !item_on_[cursor_];
            services.audio.play(AudioCue::MenuScroll);
        }
        if (screen_ == MenuScreen::Characters && (input.left_pressed || input.right_pressed)) {
            const int n=std::max(1,static_cast<int>(gallery_count("characters")));
            character_ = (character_ + (input.right_pressed?1:n-1)) % n;
            services.audio.play(AudioCue::MenuScroll);
        }
        if (screen_ == MenuScreen::SoundTest && (input.left_pressed || input.right_pressed)) {
            sound_index_ = std::clamp(sound_index_ + (input.right_pressed?1:-1), 0, 32);
            services.audio.play(AudioCue::MenuScroll);
        }
        if (input.accept_pressed || input.start_pressed) {
            const auto action=current_action();
            if (action=="one_player") { screen_ = MenuScreen::OnePlayer; cursor_ = 0; }
            else if (action=="versus") { screen_ = MenuScreen::Versus; cursor_ = 0; }
            else if (action=="options") { screen_ = MenuScreen::Options; cursor_ = 0; }
            else if (action=="data") { screen_ = MenuScreen::Data; cursor_ = 0; }
            else if (action=="css") { go_css_ = true; css_1p_ = false; }
            else if (action=="css_1p") { go_css_ = true; css_1p_ = true; }
            else if (action=="vs_options") { screen_ = MenuScreen::VSOptions; cursor_ = 0; }
            else if (action=="stereo") stereo_ = !stereo_;
            else if (action=="screen_adjust") { screen_ = MenuScreen::ScreenAdjust; cursor_ = 0; }
            else if (action=="backup_clear") { screen_ = MenuScreen::BackupClear; cursor_ = 0; }
            else if (action=="characters") { screen_ = MenuScreen::Characters; cursor_ = 0; }
            else if (action=="vs_record") { screen_ = MenuScreen::VSRecord; cursor_ = 0; }
            else if (action=="sound_test") { screen_ = MenuScreen::SoundTest; cursor_ = 0; sound_index_ = 0; }
            else if (screen_ == MenuScreen::VSOptions && cursor_ == 4) {
                screen_ = MenuScreen::ItemSwitch; cursor_ = 0;
            } else if (screen_ == MenuScreen::SoundTest) {
                services.audio.play(AudioCue::MenuSelect);
            } else if (screen_ == MenuScreen::Characters) {
                character_page_ = 1 - character_page_;
            } else {
                selected_flash_ = 8;
            }
            services.audio.play(AudioCue::MenuSelect);
        }
        if (selected_flash_ > 0) --selected_flash_;
    }

    void draw(Services& services) override {
        auto& r = services.render;
        r.begin({0,0,0,255});
        switch (screen_) {
            case MenuScreen::Main: draw_main(r); break;
            case MenuScreen::VSOptions: draw_vs_options(r); break;
            case MenuScreen::ItemSwitch: draw_item_switch(r); break;
            case MenuScreen::ScreenAdjust: draw_screen_adjust(r); break;
            case MenuScreen::SoundTest: draw_sound_test(r); break;
            case MenuScreen::VSRecord: draw_vs_record(r); break;
            case MenuScreen::BackupClear: draw_backup_clear(r); break;
            case MenuScreen::Characters: draw_characters(r); break;
            default: draw_submenu(r); break;
        }
        r.end();
    }

    std::unique_ptr<Scene> next() override {
        if (go_css_) return make_character_select_scene(stock_, team_battle_, css_1p_);
        return exit_to_title_ ? make_title_scene() : nullptr;
    }

private:
    [[nodiscard]] std::string current_action() const {
        const auto items=menu_screen_items(screen_id(screen_));
        if (cursor_>=0 && static_cast<unsigned>(cursor_)<items.size()) return items[cursor_]->action;
        return {};
    }
    [[nodiscard]] int item_count() const {
        const auto items=menu_screen_items(screen_id(screen_));
        if (!items.empty()) return static_cast<int>(items.size());
        switch (screen_) {
            case MenuScreen::VSOptions: return 5;
            case MenuScreen::ItemSwitch: return 16;
            case MenuScreen::BackupClear: return 9;
            case MenuScreen::SoundTest: return 3;
            case MenuScreen::Characters: case MenuScreen::VSRecord: case MenuScreen::ScreenAdjust: return 1;
            default: return 1;
        }
    }

    static void sprite(RenderEngine& r, std::string_view path, Vec2 position,
                       Color tint = {255,255,255,255}, Vec2 scale = {1,1}) {
        if (path.empty() || path=="-") return;
        r.sprite_at(std::string(path), position, scale, tint);
    }

    static void common_background(RenderEngine& r, std::string_view icon) {
        sprite(r,ui_path("menu.collage"),{10,10});
        sprite(r,ui_path("menu.paper"),{140,143},{160,120,20,255});
        sprite(r,ui_path("menu.paper"),{225,56},{160,120,20,255});
        sprite(r,icon,{10,10},{153,153,153,255});
        r.fill(225,143,85,87,{160,120,20,230});
        sprite(r,ui_path("menu.logo"),{235,158},{0,0,0,255});
    }

    static void option_tab(RenderEngine& r, Vec2 position, bool selected, int width = 16) {
        const Color tint = selected ? Color{255,0,40,255} : Color{130,130,170,255};
        sprite(r,ui_path("menu.tab_left"),position,tint);
        sprite(r,ui_path("menu.tab_middle"),{position.x+16,position.y},tint,
               {static_cast<float>(width),1});
        sprite(r,ui_path("menu.tab_right"),{position.x+16+width*8.0f,position.y},tint);
    }

    static void on_off(RenderEngine& r, float x, float y, bool on) {
        sprite(r,ui_path("menu.on"),{x,y},on?Color{255,0,40,255}:Color{50,50,50,255});
        sprite(r,ui_path("menu.slash"),{x+25,y},{50,50,50,255});
        sprite(r,ui_path("menu.off"),{x+32,y},on?Color{50,50,50,255}:Color{255,0,40,255});
    }

    void draw_main(RenderEngine& r) const {
        sprite(r,ui_path("menu.collage"),{10,10});
        sprite(r,ui_path("menu.main.bar_middle"),{0,37},{8,51,101,255},{12,1});
        sprite(r,ui_path("menu.main.bar_edge"),{96,37},{8,51,101,255});
        sprite(r,ui_path("menu.main.mode_select"),{28,27},{60,115,180,255});
        sprite(r,ui_path("menu.main.logo"),{226,137},{8,51,101,255});
        const auto items=menu_screen_items("main");
        for (std::size_t i=0;i<items.size();++i) {
            const bool selected = static_cast<int>(i)==cursor_;
            const auto* icon=menu_icon("main", static_cast<unsigned>(i));
            if (icon) sprite(r,selected?icon->icon:icon->icon_dark,{items[i]->tab_x,items[i]->tab_y},
                   selected?Color{255,255,255,255}:Color{150,150,150,255});
            sprite(r,items[i]->sprite,{items[i]->label_x,items[i]->label_y},{255,0,0,255});
        }
    }

    void draw_items(RenderEngine& r, std::string_view screen) const {
        const auto items=menu_screen_items(screen);
        for (std::size_t i=0;i<items.size();++i) {
            const bool selected = static_cast<int>(i)==cursor_;
            if (screen=="one_player" && i>=2) {
                sprite(r,ui_path("menu.1p_tab"),{items[i]->tab_x,items[i]->tab_y},
                       selected?Color{255,0,40,255}:Color{130,130,170,255});
            } else option_tab(r,{items[i]->tab_x,items[i]->tab_y},selected,screen=="versus"?17:16);
            sprite(r,items[i]->sprite,{items[i]->label_x,items[i]->label_y},{0,0,0,255});
        }
    }

    void draw_submenu(RenderEngine& r) const {
        const auto id=screen_id(screen_);
        if (const auto* chrome=menu_screen(id)) {
            common_background(r,chrome->icon);
            sprite(r,chrome->footer,{chrome->footer_x,chrome->footer_y},{0,0,0,255});
            if (chrome->mode_x!=0 || chrome->mode_y!=0)
                sprite(r,ui_path("menu.game_mode"),{chrome->mode_x,chrome->mode_y},{0,0,0,255});
        }
        draw_items(r,id);
        if (screen_ == MenuScreen::Versus) {
            const auto* rule = rule_==0?"menu.vs.time":rule_==2?"menu.vs.team":"menu.vs.stock";
            sprite(r,ui_path(rule),{183,78},{255,255,255,255});
            const int value = rule_==0?time_:stock_;
            if (value < 10) sprite(r,ui_numbered("menu.digit_prefix",value),{210,116},{0,0,0,255});
            else {
                sprite(r,ui_numbered("menu.digit_prefix",value/10),{202,116},{0,0,0,255});
                sprite(r,ui_numbered("menu.digit_prefix",value%10),{218,116},{0,0,0,255});
            }
        } else if (screen_ == MenuScreen::Options) {
            sprite(r,ui_path(stereo_?"menu.option.stereo":"menu.option.mono"),
                   {179,48},{0,0,0,255});
        }
        if (selected_flash_ > 0 && (selected_flash_ & 1)) r.fill(10,10,300,220,{255,255,255,45});
    }

    static void option_bubble(RenderEngine& r, float x, float y, bool selected) {
        sprite(r,ui_path("menu.vs_options.bubble"),{x,y},selected?Color{255,0,40,255}:Color{50,50,50,255});
    }

    float vs_y(int index) const {
        static constexpr std::array<const char*,5> ids{"vs.option_y0","vs.option_y1","vs.option_y2","vs.option_y3","vs.option_y4"};
        return layout_value(ids[index], static_cast<float>(61+29*index));
    }

    void draw_vs_options(RenderEngine& r) const {
        if (const auto* chrome=menu_screen("vs_options")) common_background(r,chrome->icon);
        r.fill(79,34,231,5,{128,128,128,255});
        sprite(r,ui_path("menu.vs_options.title"),{84,24},{242,199,13,255});
        option_bubble(r,114,vs_y(0),cursor_==0);
        sprite(r,ui_path("menu.vs_options.handicap"),{121,vs_y(0)+2},{0,0,0,255});
        sprite(r,ui_path("menu.off"),{191,vs_y(0)+1},
               handicap_==0?Color{255,0,40,255}:Color{50,50,50,255});
        sprite(r,ui_path("menu.on"),{221,vs_y(0)+1},
               handicap_==1?Color{255,0,40,255}:Color{50,50,50,255});
        sprite(r,ui_path("menu.auto"),{246,vs_y(0)+1},
               handicap_==2?Color{255,0,40,255}:Color{50,50,50,255});
        option_bubble(r,106,vs_y(1),cursor_==1);
        sprite(r,ui_path("menu.vs_options.team_attack"),{116,vs_y(1)+2},{0,0,0,255});
        on_off(r,212,vs_y(1)+1,team_attack_);
        option_bubble(r,98,vs_y(2),cursor_==2);
        sprite(r,ui_path("menu.vs_options.stage_select"),{104,vs_y(2)+1},{0,0,0,255});
        on_off(r,208,vs_y(2)+1,stage_select_);
        option_bubble(r,90,vs_y(3),cursor_==3);
        sprite(r,ui_path("menu.vs_options.damage"),{116,vs_y(3)+1},{0,0,0,255});
        const int hundreds=damage_/100,tens=damage_/10%10,ones=damage_%10;
        if (damage_>=100) sprite(r,ui_numbered("menu.digit_prefix",hundreds),{220,vs_y(3)},{0,0,0,255});
        if (damage_>=10) sprite(r,ui_numbered("menu.digit_prefix",tens),{232,vs_y(3)},{0,0,0,255});
        sprite(r,ui_numbered("menu.digit_prefix",ones),{244,vs_y(3)},{0,0,0,255});
        sprite(r,ui_path("menu.percentage"),{226,vs_y(3)+3},{0,0,0,255});
        option_bubble(r,82,vs_y(4),cursor_==4);
        sprite(r,ui_path("menu.vs_options.item_switch"),{128,vs_y(4)+3},{0,0,0,255});
    }

    void draw_item_switch(RenderEngine& r) const {
        sprite(r,ui_path("menu.item.decal"),{10,10},{72,42,35,255});
        r.fill(79,34,231,5,{128,128,128,255});
        sprite(r,ui_path("menu.item.label_vs"),{84,24},{242,199,13,255});
        sprite(r,ui_path("menu.item.label"),{222,30});
        sprite(r,ui_path("menu.item.list"),{125,48});
        static constexpr std::array<const char*,6> rate{"menu.item.rate.0","menu.item.rate.1","menu.item.rate.2","menu.item.rate.3","menu.item.rate.4","menu.item.rate.5"};
        static constexpr std::array<const char*,6> xs{"menu.item_rate_x0","menu.item_rate_x1","menu.item_rate_x2","menu.item_rate_x3","menu.item_rate_x4","menu.item_rate_x5"};
        sprite(r,ui_path(rate[item_rate_]),{layout_value(xs[item_rate_],242),49},{255,0,0,255});
        for (int i=1;i<16;++i) {
            const float y=static_cast<float>(i*10+54);
            sprite(r,ui_path("menu.item.toggle_on"),{244,y},item_on_[i]?Color{255,0,40,255}:Color{50,50,50,255});
            sprite(r,ui_path("menu.item.toggle_slash"),{265,y},{50,50,50,255});
            sprite(r,ui_path("menu.item.toggle_off"),{270,y},item_on_[i]?Color{50,50,50,255}:Color{255,0,40,255});
        }
        const float cy = cursor_==0?47.f:static_cast<float>(cursor_*10+51);
        sprite(r,ui_path("menu.item.cursor"),{115,cy},{255,222,0,255});
    }

    void draw_screen_adjust(RenderEngine& r) const {
        sprite(r,ui_path("menu.adjust.guide"),{10+adjust_x_,10+adjust_y_});
        sprite(r,ui_path("menu.adjust.instruction"),{40,200});
    }

    void draw_sound_test(RenderEngine& r) const {
        if (const auto* chrome=menu_screen("sound_test")) common_background(r,chrome->icon);
        sprite(r,ui_path("menu.sound.title"),{84,24},{242,199,13,255});
        static constexpr std::array<const char*,3> rows{"menu.sound.music","menu.sound.sound","menu.sound.voice"};
        for (int i=0;i<3;++i) {
            const float y=70.f+i*32;
            option_tab(r,{90,y},cursor_==i,18);
            sprite(r,ui_path(rows[i]),{120,y+4},{0,0,0,255});
        }
        sprite(r,ui_path("menu.sound.play"),{90,180});
        sprite(r,ui_path("menu.sound.exit"),{90,196});
        sprite(r,ui_numbered("menu.digit_prefix",sound_index_/10),{210,74.f+cursor_*32},{0,0,0,255});
        sprite(r,ui_numbered("menu.digit_prefix",sound_index_%10),{226,74.f+cursor_*32},{0,0,0,255});
    }

    void draw_vs_record(RenderEngine& r) const {
        sprite(r,ui_path("menu.record.wallpaper"),{10,10});
        sprite(r,ui_path("menu.record.label"),{84,16});
        const unsigned count=gallery_count("record");
        for (unsigned i=0;i<count;++i) {
            const float x=18.f+(i%6)*48, y=50.f+(i/6)*40;
            sprite(r,ui_path("menu.record.prefix")+gallery_key("record",i)+ui_path("menu.record.icon_suffix"),{x,y});
        }
        sprite(r,ui_path("menu.record.kos"),{24,140});
        sprite(r,ui_path("menu.record.tko"),{24,156});
        sprite(r,ui_path("menu.record.win"),{24,172});
        sprite(r,ui_path("menu.record.digit0"),{180,140});
        sprite(r,ui_path("menu.record.digit0"),{180,156});
        sprite(r,ui_path("menu.record.digit0"),{180,172});
    }

    void draw_backup_clear(RenderEngine& r) const {
        if (const auto* chrome=menu_screen("backup_clear")) common_background(r,chrome->icon);
        sprite(r,ui_path("menu.backup.header"),{84,24},{242,199,13,255});
        for (int i=0;i<9;++i) {
            const float y=48.f+i*16;
            if (i==cursor_) r.fill(80,y-1,200,14,{255,0,40,80});
            sprite(r,ui_path("menu.backup."+std::to_string(i)),{90,y},{0,0,0,255});
        }
        if (selected_flash_>0) {
            sprite(r,ui_path("menu.backup.sure"),{90,200});
            sprite(r,ui_path("menu.backup.okay"),{90,214});
        }
    }

    void draw_characters(RenderEngine& r) const {
        const auto key=gallery_key("characters", static_cast<unsigned>(character_));
        sprite(r,ui_path(character_page_?"menu.characters.works":"menu.characters.story"),{10,10});
        sprite(r,ui_path("menu.characters.label"),{20,16});
        sprite(r,ui_path("menu.characters.prefix")+key+"Name.png",{20,36});
        sprite(r,ui_path("menu.characters.prefix")+key+(character_page_?"Works.png":"Story.png"),{20,70});
        if (const auto* kind=css_kind(key=="Purin"?"PURIN":[] (std::string s) {
            for (auto& ch:s) ch=static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
            return s;
        }(key))) sprite(r,kind->portrait,{220,40});
        sprite(r,ui_path("menu.arrow_l"),{16,200});
        sprite(r,ui_path("menu.arrow_r"),{280,200});
    }

    MenuScreen screen_{MenuScreen::Main};
    int cursor_{}, tic_{}, selected_flash_{};
    int stock_{3}, time_{8}, rule_{1}, handicap_{}, damage_{}, item_rate_{3};
    int character_{}, character_page_{}, sound_index_{};
    float adjust_x_{}, adjust_y_{};
    std::array<bool,16> item_on_{true,true,true,true,true,true,true,true,true,true,true,true,true,true,true,true};
    bool stereo_{true}, team_battle_{}, team_attack_{true}, stage_select_{true};
    bool exit_to_title_{}, go_css_{}, css_1p_{};
};

} // namespace

std::unique_ptr<Scene> make_menu_scene() { return std::make_unique<MenuScene>(); }

} // namespace sagas
