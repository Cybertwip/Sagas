#include <sagas/Engine.hpp>

#include <algorithm>
#include <array>

namespace sagas {
namespace {

enum class MenuScreen { Main, OnePlayer, Versus, Options, Data };

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

class MenuScene final : public Scene {
public:
    void update(Services& services, const InputState& input, float) override {
        ++tic_;
        const auto count = item_count();
        const bool previous = input.up_pressed || input.right_pressed;
        const bool next = input.down_pressed || input.left_pressed;
        if (previous != next) {
            cursor_ = previous ? (cursor_ + count - 1) % count : (cursor_ + 1) % count;
            services.audio.play(AudioCue::MenuScroll);
        }
        if (input.cancel_pressed) {
            if (screen_ == MenuScreen::Main) exit_to_title_ = true;
            else { screen_ = MenuScreen::Main; cursor_ = 0; }
        }
        if (input.accept_pressed || input.start_pressed) {
            if (screen_ == MenuScreen::Main) {
                screen_ = static_cast<MenuScreen>(cursor_ + 1);
                cursor_ = 0;
            } else if (screen_ == MenuScreen::Versus && cursor_ == 0) {
                go_css_ = true;
                css_1p_ = false;
            } else if (screen_ == MenuScreen::OnePlayer && cursor_ == 0) {
                go_css_ = true;
                css_1p_ = true;
            } else {
                selected_flash_ = 8;
                if (screen_ == MenuScreen::Options && cursor_ == 0) stereo_ = !stereo_;
                if (screen_ == MenuScreen::Versus && cursor_ == 1) team_battle_ = !team_battle_;
            }
            services.audio.play(AudioCue::MenuSelect);
        }
        if (screen_ == MenuScreen::Versus && cursor_ == 2 && (input.left_pressed || input.right_pressed))
            stock_ = std::clamp(stock_ + (input.right_pressed ? 1 : -1), 1, 99);
        if (selected_flash_ > 0) --selected_flash_;
    }

    void draw(Services& services) override {
        auto& r = services.render;
        r.begin({0,0,0,255});
        if (screen_ == MenuScreen::Main) draw_main(r);
        else draw_submenu(r);
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
        // The original label display list completes the lower paper wedge
        // with a translucent primitive rectangle.  Omitting this exposed the
        // collage through the decal and made the bottom-right look like a
        // corrupt texture atlas.
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
            sprite(r,team_battle_?"MNVSMode/TeamText.png":"MNVSMode/StockText.png",
                   {183,78},{255,255,255,255});
            const auto digit = "MNCommon/Digit" + std::to_string(stock_) + ".png";
            if (stock_ < 10) sprite(r,digit,{210,116},{0,0,0,255});
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

    MenuScreen screen_{MenuScreen::Main};
    int cursor_{}, tic_{}, selected_flash_{};
    int stock_{3};
    bool stereo_{true}, team_battle_{}, exit_to_title_{}, go_css_{}, css_1p_{};
};

} // namespace

std::unique_ptr<Scene> make_menu_scene() { return std::make_unique<MenuScene>(); }

} // namespace sagas
