#pragma once
#include <sagas/Engine.hpp>
#include <sagas/Fighter.hpp>
#include <string>
#include <vector>
namespace sagas {
struct CssEntry {
    FighterKind kind{FighterKind::Mario};
    std::string portrait, model, name;
};
struct CssLayout {
    float logical_w{640}, logical_h{360}, cell{48}, start_x{80}, start_y{40};
    float card_x0{28}, card_step{152}, card_y{200}, card_w{132}, card_h{150};
    float camera_z{5000}, fighter_y{-850}, cell_1p_w{45}, cell_1p_h{43};
    int columns{10}, visible_rows{3};
    static CssLayout load();
};
struct CssRoster {
    std::vector<CssEntry> entries;
    static CssRoster build(Services& services, bool one_player);
};
std::string_view css_card_name(FighterKind kind);
std::string css_portrait_path(std::string_view key, FighterKind kind);
void draw_name_label(RenderEngine& render, std::string_view name, float x, float y, float width, float height, Color color={255,255,255,255});
} // namespace sagas
