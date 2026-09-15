#pragma once
#include <sagas/Scene.hpp>
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
    float camera_z{5000}, fighter_y{-850};
    float cell_1p_w{45}, cell_1p_h{43}, start_1p_x{25}, start_1p_y{36};
    float logical_1p_w{320}, logical_1p_h{240};
    float card_1p_x0{22}, card_1p_step{69}, card_1p_y{131};
    float camera_fov{30}, camera_aspect_w{45}, camera_aspect_h{44};
    int columns{10}, visible_rows{3}, columns_1p{6};
    static CssLayout load();
    [[nodiscard]] float width(bool one_player) const { return one_player ? logical_1p_w : logical_w; }
    [[nodiscard]] float height(bool one_player) const { return one_player ? logical_1p_h : logical_h; }
    [[nodiscard]] float portrait_w(bool one_player) const { return one_player ? cell_1p_w : cell; }
    [[nodiscard]] float portrait_h(bool one_player) const { return one_player ? cell_1p_h : cell; }
    [[nodiscard]] float card_x(bool one_player, int player) const {
        return (one_player ? card_1p_x0 : card_x0) + player * (one_player ? card_1p_step : card_step);
    }
    [[nodiscard]] float card_top(bool one_player) const { return one_player ? card_1p_y : card_y; }
};
struct CssRoster {
    std::vector<CssEntry> entries;
    static CssRoster build(Services& services, bool one_player);
};
std::string_view css_card_name(FighterKind kind);
std::string css_portrait_path(std::string_view key, FighterKind kind);
void draw_name_label(RenderEngine& render, std::string_view name, float x, float y, float width, float height, Color color={255,255,255,255});
} // namespace sagas
