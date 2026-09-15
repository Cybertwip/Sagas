#pragma once
#include <sagas/Fighter.hpp>
#include <sagas/Render.hpp>
#include <span>
namespace sagas {
struct BattleHudState {
    std::span<const FighterBody> bodies;
    std::span<const int> kos;
    int winner{-1};
    int intro_tics{};
};
void draw_battle_stage(RenderEngine& render, bool finished, int winner);
void draw_battle_hud(RenderEngine& render, const BattleHudState& state);
void draw_battle_results(RenderEngine& render, const BattleHudState& state);
} // namespace sagas
