#include <sagas/SceneDescriptors.hpp>
#include <sagas/CssRoster.hpp>
#include <cassert>
#include <string>

int main() {
    using namespace sagas;
    assert(!ui_sprites.empty());
    assert(ui_path("css.background") == "textures/MNSelectCommon/StoneBackground.png");
    assert(ui_path("menu.collage") == "textures/MNCommon/SmashBrosCollage.png");
    assert(ui_path("title.smash") == "textures/MNTitle/Smash.png");
    assert(layout_value("css.columns") == 10);
    assert(layout_value("css.logical_w") == 640);
    assert(css_slots.size() == 30);
    assert(css_slots[0].key == "MARINA");
    assert(css_kind("MARIO") && css_kind("MARIO")->kind == 1);
    assert(stock_dir(FighterKind::Mario) == "MarioModel");
    const auto vs = menu_screen_items("versus");
    assert(vs.size() == 4);
    assert(vs[0]->action == "css");
    assert(menu_screen("one_player") && menu_screen("one_player")->footer_x == 161);
    assert(menu_icon("main", 0) && menu_icon("main", 0)->icon.find("ControllerIcon") != std::string::npos);
    assert(gallery_count("characters") == 12);
    assert(gallery_key("record", 1) == "Fox");
    bool mia=false;
    for (const auto& hd : hd_models) if (hd.key == "MIA") {
        mia = true;
        assert(hd.parent == "Luigi");
        assert(hd.fbx == "HD/Mia.fbx");
    }
    assert(mia);
    const auto layout = CssLayout::load();
    assert(layout.columns == 10);
    assert(layout.cell == 48);
    assert(css_card_name(FighterKind::Mario).find("MarioText") != std::string_view::npos);
}
