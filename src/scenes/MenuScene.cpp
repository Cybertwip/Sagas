#include <sagas/Engine.hpp>

#include <algorithm>
#include <array>
#include <string>

namespace sagas {
namespace {

enum class MenuScreen {
    Main, OnePlayer, Versus, Options, Data,
    VSOptions, ItemSwitch, ScreenAdjust, SoundTest, VSRecord, BackupClear, Characters
};

struct MenuItem {
    std::string_view texture;
    Vec2 tab;
    Vec2 label;
};

constexpr std::array<MenuItem,4> one_player_items{{
    {"MN1P/1PGameText.png", {124,42}, {161,46}},
    {"MN1P/TrainingModeText.png", {99,84}, {107,87}},
    {"MN1P/Bonus1PracticeText.png", {78,126}, {97,127}},
    {"MN1P/Bonus2PracticeText.png", {67,148}, {86,149}},
}};
constexpr std::array<MenuItem,4> versus_items{{
    {"MNVSMode/VSStartText.png", {120,31}, {153,36}},
    {"MNVSMode/RulePeriodText.png", {97,70}, {108,75}},
    {"MNVSMode/StockPeriodText.png", {74,109}, {106,114}},
    {"MNVSMode/VSOptionsText.png", {51,148}, {71,151}},
}};
constexpr std::array<MenuItem,3> option_items{{
    {"MNOption/SoundText.png", {113,42}, {116,46}},
    {"MNOption/ScreenAdjustText.png", {91,89}, {103,92}},
    {"MNOption/BackupClearText.png", {69,136}, {86,140}},
}};
constexpr std::array<MenuItem,3> data_items{{
    {"MNData/CharactersText.png", {133,42}, {159,46}},
    {"MNData/VSRecordText.png", {101,89}, {128,93}},
    {"MNData/SoundTestText.png", {69,136}, {95,140}},
}};
constexpr std::array<int,5> kVSOptionY{61,90,119,148,176};
constexpr std::array<const char*,6> kItemRate{
    "MNVSItemSwitch/AppearanceNone.png","MNVSItemSwitch/AppearanceVeryLow.png",
    "MNVSItemSwitch/AppearanceLow.png","MNVSItemSwitch/AppearanceMiddle.png",
    "MNVSItemSwitch/AppearanceHigh.png","MNVSItemSwitch/AppearanceVeryHigh.png"};
constexpr std::array<int,6> kItemRateX{242,240,254,244,252,238};
constexpr std::array<std::string_view,12> kRecordIcons{
    "Mario","Fox","Donkey","Samus","Luigi","Link","Yoshi","Captain","Kirby","Pikachu","Purin","Ness"};
constexpr std::array<std::string_view,12> kCharacterKeys{
    "Mario","Donkey","Fox","Kirby","Link","Luigi","Ness","Pikachu","Purin","Samus","Yoshi","Captain"};
constexpr std::array<const char*,9> kBackupOptions{
    "OptionVSRecord.png","OptionBonusStageTime.png","Option1PHighScore.png","OptionPrize.png",
    "OptionNewcomers.png","OptionSubjectMode.png","OptionAllDataClear.png","OptionYes.png","OptionNo.png"};

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
            character_ = (character_ + (input.right_pressed?1:11)) % 12;
            services.audio.play(AudioCue::MenuScroll);
        }
        if (screen_ == MenuScreen::SoundTest && (input.left_pressed || input.right_pressed)) {
            sound_index_ = std::clamp(sound_index_ + (input.right_pressed?1:-1), 0, 32);
            services.audio.play(AudioCue::MenuScroll);
        }
        if (input.accept_pressed || input.start_pressed) {
            if (screen_ == MenuScreen::Main) {
                screen_ = static_cast<MenuScreen>(cursor_ + 1);
                cursor_ = 0;
            } else if (screen_ == MenuScreen::Versus && cursor_ == 0) {
                go_css_ = true;
                css_1p_ = false;
            } else if (screen_ == MenuScreen::Versus && cursor_ == 3) {
                screen_ = MenuScreen::VSOptions; cursor_ = 0;
            } else if (screen_ == MenuScreen::OnePlayer && cursor_ == 0) {
                go_css_ = true;
                css_1p_ = true;
            } else if (screen_ == MenuScreen::Options && cursor_ == 0) {
                stereo_ = !stereo_;
            } else if (screen_ == MenuScreen::Options && cursor_ == 1) {
                screen_ = MenuScreen::ScreenAdjust; cursor_ = 0;
            } else if (screen_ == MenuScreen::Options && cursor_ == 2) {
                screen_ = MenuScreen::BackupClear; cursor_ = 0;
            } else if (screen_ == MenuScreen::Data && cursor_ == 0) {
                screen_ = MenuScreen::Characters; cursor_ = 0;
            } else if (screen_ == MenuScreen::Data && cursor_ == 1) {
                screen_ = MenuScreen::VSRecord; cursor_ = 0;
            } else if (screen_ == MenuScreen::Data && cursor_ == 2) {
                screen_ = MenuScreen::SoundTest; cursor_ = 0; sound_index_ = 0;
            } else if (screen_ == MenuScreen::VSOptions && cursor_ == 4) {
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
    [[nodiscard]] int item_count() const {
        switch (screen_) {
            case MenuScreen::Main: case MenuScreen::OnePlayer: case MenuScreen::Versus: return 4;
            case MenuScreen::Options: case MenuScreen::Data: return 3;
            case MenuScreen::VSOptions: return 5;
            case MenuScreen::ItemSwitch: return 16;
            case MenuScreen::BackupClear: return 9;
            case MenuScreen::SoundTest: return 3;
            case MenuScreen::Characters: case MenuScreen::VSRecord: case MenuScreen::ScreenAdjust: return 1;
        }
        return 1;
    }

    static void sprite(RenderEngine& r, std::string_view name, Vec2 position,
                       Color tint = {255,255,255,255}, Vec2 scale = {1,1}) {
        r.sprite_at("textures/" + std::string(name), position, scale, tint);
    }

    static void common_background(RenderEngine& r, std::string_view icon) {
        sprite(r,"MNCommon/SmashBrosCollage.png",{10,10});
        sprite(r,"MNCommon/DecalPaper.png",{140,143},{160,120,20,255});
        sprite(r,"MNCommon/DecalPaper.png",{225,56},{160,120,20,255});
        sprite(r,icon,{10,10},{153,153,153,255});
        r.fill(225,143,85,87,{160,120,20,230});
        sprite(r,"MNCommon/SmashLogo.png",{235,158},{0,0,0,255});
    }

    static void option_tab(RenderEngine& r, Vec2 position, bool selected, int width = 16) {
        const Color tint = selected ? Color{255,0,40,255} : Color{130,130,170,255};
        sprite(r,"MNCommon/OptionTabLeft.png",position,tint);
        sprite(r,"MNCommon/OptionTabMiddle.png",{position.x+16,position.y},tint,
               {static_cast<float>(width),1});
        sprite(r,"MNCommon/OptionTabRight.png",{position.x+16+width*8.0f,position.y},tint);
    }

    static void on_off(RenderEngine& r, float x, float y, bool on) {
        sprite(r,"MNCommon/OnText.png",{x,y},on?Color{255,0,40,255}:Color{50,50,50,255});
        sprite(r,"MNCommon/Slash.png",{x+25,y},{50,50,50,255});
        sprite(r,"MNCommon/OffText.png",{x+32,y},on?Color{50,50,50,255}:Color{255,0,40,255});
    }

    void draw_main(RenderEngine& r) const {
        sprite(r,"MNCommon/SmashBrosCollage.png",{10,10});
        sprite(r,"MNMain/DecalBarMiddle.png",{0,37},{8,51,101,255},{12,1});
        sprite(r,"MNMain/DecalBarEdge.png",{96,37},{8,51,101,255});
        sprite(r,"MNMain/ModeSelectText.png",{28,27},{60,115,180,255});
        sprite(r,"MNMain/SmashLogo.png",{226,137},{8,51,101,255});

        struct MainItem { std::string_view normal, dark, label; Vec2 icon, text; };
        static constexpr std::array<MainItem,4> items{{
            {"MNMain/ControllerIcon.png","MNMain/ControllerIconDark.png","MNMain/1PModeText.png",{169,27},{224,52}},
            {"MNMain/ConsoleIcon.png","MNMain/ConsoleIconDark.png","MNMain/VsModeText.png",{128,64},{183,89}},
            {"MNMain/SettingsIcon.png","MNMain/SettingsIconDark.png","MNMain/OptionText.png",{87,101},{142,126}},
            {"MNMain/DataIcon.png","MNMain/DataIconDark.png","MNMain/DataText.png",{46,138},{102,163}},
        }};
        for (std::size_t i=0;i<items.size();++i) {
            const bool selected = static_cast<int>(i)==cursor_;
            sprite(r,selected?items[i].normal:items[i].dark,items[i].icon,
                   selected?Color{255,255,255,255}:Color{150,150,150,255});
            sprite(r,items[i].label,items[i].text,{255,0,0,255});
        }
    }

    template<std::size_t N>
    void draw_items(RenderEngine& r, const std::array<MenuItem,N>& items) const {
        for (std::size_t i=0;i<items.size();++i) {
            const bool selected = static_cast<int>(i)==cursor_;
            if (screen_==MenuScreen::OnePlayer && i>=2) {
                sprite(r,"MN1P/OptionTab.png",items[i].tab,
                       selected?Color{255,0,40,255}:Color{130,130,170,255});
            } else option_tab(r,items[i].tab,selected,screen_==MenuScreen::Versus?17:16);
            sprite(r,items[i].texture,items[i].label,{0,0,0,255});
        }
    }

    void draw_submenu(RenderEngine& r) const {
        if (screen_ == MenuScreen::OnePlayer) {
            common_background(r,"MN1P/ControllerIconDark.png");
            sprite(r,"MN1P/1PText.png",{161,194},{0,0,0,255});
            sprite(r,"MNCommon/GameModeText.png",{188,88},{0,0,0,255});
            draw_items(r,one_player_items);
        } else if (screen_ == MenuScreen::Versus) {
            common_background(r,"MNVSMode/ConsoleIconDark.png");
            sprite(r,"MNVSMode/VSText.png",{158,192},{0,0,0,255});
            sprite(r,"MNCommon/GameModeText.png",{189,87},{0,0,0,255});
            draw_items(r,versus_items);
            const auto* rule = rule_==0?"MNVSMode/TimeText.png":rule_==2?"MNVSMode/TeamText.png":"MNVSMode/StockText.png";
            sprite(r,rule,{183,78},{255,255,255,255});
            const int value = rule_==0?time_:stock_;
            if (value < 10) sprite(r,"MNCommon/Digit"+std::to_string(value)+".png",{210,116},{0,0,0,255});
            else {
                sprite(r,"MNCommon/Digit"+std::to_string(value/10)+".png",{202,116},{0,0,0,255});
                sprite(r,"MNCommon/Digit"+std::to_string(value%10)+".png",{218,116},{0,0,0,255});
            }
        } else if (screen_ == MenuScreen::Options) {
            common_background(r,"MNOption/SettingsIconDark.png");
            sprite(r,"MNOption/OptionText.png",{201,120},{0,0,0,255});
            draw_items(r,option_items);
            sprite(r,stereo_?"MNOption/StereoText.png":"MNOption/MonoText.png",
                   {179,48},{0,0,0,255});
        } else {
            common_background(r,"MNData/DataIconDark.png");
            sprite(r,"MNData/DataText.png",{206,131},{0,0,0,255});
            draw_items(r,data_items);
        }
        if (selected_flash_ > 0 && (selected_flash_ & 1)) r.fill(10,10,300,220,{255,255,255,45});
    }

    static void option_bubble(RenderEngine& r, float x, float y, bool selected) {
        sprite(r,"MNVSOptions/Bubble.png",{x,y},selected?Color{255,0,40,255}:Color{50,50,50,255});
    }

    void draw_vs_options(RenderEngine& r) const {
        common_background(r,"MNVSOptions/ConsoleIconDark.png");
        r.fill(79,34,231,5,{128,128,128,255});
        sprite(r,"MNVSOptions/VSOptionsText.png",{84,24},{242,199,13,255});
        option_bubble(r,114,static_cast<float>(kVSOptionY[0]),cursor_==0);
        sprite(r,"MNVSOptions/HandicapText.png",{121,static_cast<float>(kVSOptionY[0]+2)},{0,0,0,255});
        sprite(r,"MNCommon/OffText.png",{191,static_cast<float>(kVSOptionY[0]+1)},
               handicap_==0?Color{255,0,40,255}:Color{50,50,50,255});
        sprite(r,"MNCommon/OnText.png",{221,static_cast<float>(kVSOptionY[0]+1)},
               handicap_==1?Color{255,0,40,255}:Color{50,50,50,255});
        sprite(r,"MNCommon/AutoText.png",{246,static_cast<float>(kVSOptionY[0]+1)},
               handicap_==2?Color{255,0,40,255}:Color{50,50,50,255});
        option_bubble(r,106,static_cast<float>(kVSOptionY[1]),cursor_==1);
        sprite(r,"MNVSOptions/TeamAttackText.png",{116,static_cast<float>(kVSOptionY[1]+2)},{0,0,0,255});
        on_off(r,212,static_cast<float>(kVSOptionY[1]+1),team_attack_);
        option_bubble(r,98,static_cast<float>(kVSOptionY[2]),cursor_==2);
        sprite(r,"MNVSOptions/StageSelectText.png",{104,static_cast<float>(kVSOptionY[2]+1)},{0,0,0,255});
        on_off(r,208,static_cast<float>(kVSOptionY[2]+1),stage_select_);
        option_bubble(r,90,static_cast<float>(kVSOptionY[3]),cursor_==3);
        sprite(r,"MNVSOptions/DamageText.png",{116,static_cast<float>(kVSOptionY[3]+1)},{0,0,0,255});
        const int hundreds=damage_/100,tens=damage_/10%10,ones=damage_%10;
        if (damage_>=100) sprite(r,"MNCommon/Digit"+std::to_string(hundreds)+".png",{220,static_cast<float>(kVSOptionY[3])},{0,0,0,255});
        if (damage_>=10) sprite(r,"MNCommon/Digit"+std::to_string(tens)+".png",{232,static_cast<float>(kVSOptionY[3])},{0,0,0,255});
        sprite(r,"MNCommon/Digit"+std::to_string(ones)+".png",{244,static_cast<float>(kVSOptionY[3])},{0,0,0,255});
        sprite(r,"MNCommon/Percentage.png",{226,static_cast<float>(kVSOptionY[3]+3)},{0,0,0,255});
        option_bubble(r,82,static_cast<float>(kVSOptionY[4]),cursor_==4);
        sprite(r,"MNVSOptions/ItemSwitchText.png",{128,static_cast<float>(kVSOptionY[4]+3)},{0,0,0,255});
    }

    void draw_item_switch(RenderEngine& r) const {
        sprite(r,"MNVSItemSwitch/DecalButton.png",{10,10},{72,42,35,255});
        r.fill(79,34,231,5,{128,128,128,255});
        sprite(r,"MNVSItemSwitch/LabelVSOptions.png",{84,24},{242,199,13,255});
        sprite(r,"MNVSItemSwitch/LabelItemSwitch.png",{222,30});
        sprite(r,"MNVSItemSwitch/ItemList.png",{125,48});
        sprite(r,kItemRate[item_rate_],{static_cast<float>(kItemRateX[item_rate_]),49},{255,0,0,255});
        for (int i=1;i<16;++i) {
            const float y=static_cast<float>(i*10+54);
            sprite(r,"MNVSItemSwitch/ToggleOn.png",{244,y},item_on_[i]?Color{255,0,40,255}:Color{50,50,50,255});
            sprite(r,"MNVSItemSwitch/ToggleSlash.png",{265,y},{50,50,50,255});
            sprite(r,"MNVSItemSwitch/ToggleOff.png",{270,y},item_on_[i]?Color{50,50,50,255}:Color{255,0,40,255});
        }
        const float cy = cursor_==0?47.f:static_cast<float>(cursor_*10+51);
        sprite(r,"MNVSItemSwitch/Cursor.png",{115,cy},{255,222,0,255});
    }

    void draw_screen_adjust(RenderEngine& r) const {
        sprite(r,"MNScreenAdjust/Guide.png",{10+adjust_x_,10+adjust_y_});
        sprite(r,"MNScreenAdjust/Instruction.png",{40,200});
    }

    void draw_sound_test(RenderEngine& r) const {
        common_background(r,"MNData/DataIconDark.png");
        sprite(r,"MNSoundTest/SoundTestText.png",{84,24},{242,199,13,255});
        const std::array<std::string_view,3> rows{"MNSoundTest/MusicText.png","MNSoundTest/SoundText.png","MNSoundTest/VoiceText.png"};
        for (int i=0;i<3;++i) {
            const float y=70.f+i*32;
            option_tab(r,{90,y},cursor_==i,18);
            sprite(r,rows[i],{120,y+4},{0,0,0,255});
        }
        sprite(r,"MNSoundTest/ColonPlayText.png",{90,180});
        sprite(r,"MNSoundTest/ColonExitText.png",{90,196});
        sprite(r,"MNCommon/Digit"+std::to_string(sound_index_/10)+".png",{210,74.f+cursor_*32},{0,0,0,255});
        sprite(r,"MNCommon/Digit"+std::to_string(sound_index_%10)+".png",{226,74.f+cursor_*32},{0,0,0,255});
    }

    void draw_vs_record(RenderEngine& r) const {
        sprite(r,"MNVSRecordMain/PortraitWallpaper.png",{10,10});
        sprite(r,"MNVSRecordMain/Label.png",{84,16});
        for (int i=0;i<12;++i) {
            const float x=18.f+(i%6)*48, y=50.f+(i/6)*40;
            sprite(r,"MNVSRecordMain/"+std::string(kRecordIcons[i])+"IconColor.png",{x,y});
        }
        sprite(r,"MNVSRecordMain/LabelKOs.png",{24,140});
        sprite(r,"MNVSRecordMain/LabelTKO.png",{24,156});
        sprite(r,"MNVSRecordMain/LabelWinPercent.png",{24,172});
        sprite(r,"MNVSRecordMain/Digit0.png",{180,140});
        sprite(r,"MNVSRecordMain/Digit0.png",{180,156});
        sprite(r,"MNVSRecordMain/Digit0.png",{180,172});
    }

    void draw_backup_clear(RenderEngine& r) const {
        common_background(r,"MNOption/SettingsIconDark.png");
        sprite(r,"MNBackupClear/HeaderBackupClear.png",{84,24},{242,199,13,255});
        for (int i=0;i<9;++i) {
            const float y=48.f+i*16;
            if (i==cursor_) r.fill(80,y-1,200,14,{255,0,40,80});
            sprite(r,std::string("MNBackupClear/")+kBackupOptions[i],{90,y},{0,0,0,255});
        }
        if (selected_flash_>0) {
            sprite(r,"MNBackupClear/AreYouSureText.png",{90,200});
            sprite(r,"MNBackupClear/IsOkayText.png",{90,214});
        }
    }

    void draw_characters(RenderEngine& r) const {
        const auto key=std::string(kCharacterKeys[character_]);
        sprite(r,character_page_?"MNCharacters/WorksWallpaper.png":"MNCharacters/StoryWallpaper.png",{10,10});
        sprite(r,"MNCharacters/Label.png",{20,16});
        sprite(r,"MNCharacters/"+key+"Name.png",{20,36});
        sprite(r,"MNCharacters/"+key+(character_page_?"Works.png":"Story.png"),{20,70});
        sprite(r,"MNPlayersPortraits/"+key+".png",{220,40});
        sprite(r,"MNCommon/ArrowL.png",{16,200});
        sprite(r,"MNCommon/ArrowR.png",{280,200});
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
