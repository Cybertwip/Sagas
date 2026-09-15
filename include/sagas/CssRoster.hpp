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
struct CssRoster {
    std::vector<CssEntry> entries;
    static CssRoster build(Services& services, bool one_player);
};
std::string_view css_card_name(FighterKind kind);
std::string css_portrait_path(std::string_view key, FighterKind kind);
void draw_name_label(RenderEngine& render, std::string_view name, float x, float y, float width, float height, Color color={255,255,255,255});
} // namespace sagas
