#include <sagas/BattleHud.hpp>
#include <sagas/SceneDescriptors.hpp>
#include <sagas/Fighter.hpp>
#include <algorithm>
#include <array>
#include <cctype>
#include <string>

namespace sagas {
namespace {

constexpr std::array<int,12> kHudDigitWidth{14,9,15,14,15,13,15,14,15,15,17,20};
constexpr std::array<Color,4> kPlayerTint{{{255,80,80,255},{80,110,255,255},{255,210,50,255},{60,200,90,255}}};
constexpr std::array<Color,4> kResultsEnv{{{152,111,108,255},{134,134,209,255},{155,142,108,255},{113,130,120,255}}};

float damage_x(unsigned player) {
    static constexpr std::array<const char*,4> ids{"hud.damage_x0","hud.damage_x1","hud.damage_x2","hud.damage_x3"};
    return layout_value(ids[std::min(player,3U)], static_cast<float>(55+70*std::min(player,3U)));
}

float results_column_x(unsigned player, unsigned count) {
    if (count<=2) { constexpr float x[]{135,215}; return x[std::min(player,1U)]; }
    if (count==3) { constexpr float x[]{125,175,225}; return x[std::min(player,2U)]; }
    constexpr float x[]{115,155,195,235}; return x[std::min(player,3U)];
}

std::string stock_sprite(FighterKind kind) {
    return std::string(ui_path("hud.texture_root"))+std::string(stock_dir(kind))+ui_path("hud.stock_suffix");
}

std::string emblem_sprite(FighterKind kind) {
    return std::string(ui_path("hud.texture_root"))+std::string(stock_dir(kind))+ui_path("hud.emblem_suffix");
}

void draw_percent(RenderEngine& r,unsigned player,int damage) {
    const float origin=damage_x(player);
    const float y=layout_value("hud.damage_y",210);
    std::array<int,4> glyphs{};
    int shown=0;
    if (damage>=100) glyphs[shown++]=damage/100%10;
    if (damage>=10) glyphs[shown++]=damage/10%10;
    glyphs[shown++]=damage%10;
    glyphs[shown++]=10;
    float total=0;
    for (int i=0;i<shown;++i) total+=kHudDigitWidth[glyphs[i]];
    float x=origin-total*0.5f;
    for (int i=0;i<shown;++i) {
        const int glyph=glyphs[i];
        const auto path=glyph==10?ui_path("hud.percent"):ui_numbered("hud.digit_prefix",glyph);
        r.sprite_at(path,{x,y});
        x+=kHudDigitWidth[glyph];
    }
}

void custom_results_name(RenderEngine& r,std::string_view name,float x,float y) {
    const auto font=std::string(ui_path("css.font"));
    for (unsigned char ch:name) {
        ch=static_cast<unsigned char>(std::toupper(ch));
        if (ch>='A' && ch<='Z') {
            r.sprite_at(font+std::string(1,static_cast<char>(ch))+".png",{x,y});
            x+=10;
        } else x+=6;
    }
}

}

void draw_battle_stage(RenderEngine& r, bool finished, int winner) {
    if (finished) {
        const int tint=winner>=0?winner%4:0;
        r.sprite_at(ui_path("results.wallpaper"),{10,10},{1,1},kResultsEnv[tint]);
    } else {
        r.sprite_rect(ui_path("battle.stage"),0,0,320,240);
    }
}

void draw_battle_hud(RenderEngine& r, const BattleHudState& state) {
    const float stock_y=layout_value("hud.stock_y",185);
    for (unsigned i=0;i<state.bodies.size() && i<4;++i) {
        const float origin=damage_x(i);
        r.sprite_at(emblem_sprite(state.bodies[i].kind),
                    {origin-10.5f,194.5f},{1,1},kPlayerTint[i]);
        const int stocks=std::max(0,state.bodies[i].stocks);
        const auto stock=stock_sprite(state.bodies[i].kind);
        if (stocks<=6) {
            for (int s=0;s<stocks;++s)
                r.sprite_at(stock,{origin-28.f+s*10.f,stock_y});
        } else {
            r.sprite_at(stock,{origin-28.f,stock_y});
            r.sprite_at(ui_path("hud.cross"),{origin-14.f,stock_y});
            r.sprite_at(ui_numbered("hud.status_digit_prefix",std::min(stocks,9)),{origin-4.f,stock_y});
        }
        draw_percent(r,i,std::min(999,static_cast<int>(state.bodies[i].damage)));
    }
    if (state.intro_tics>0 && state.intro_tics<78) {
        const float pulse=state.intro_tics>24?1.f:state.intro_tics/24.f;
        const Color tint{255,255,255,static_cast<std::uint8_t>(255*pulse)};
        const float y=layout_value("hud.go_y",93);
        r.sprite_at(ui_path("hud.go_g"),{layout_value("hud.go_g_x",82),y},{1,1},tint);
        r.sprite_at(ui_path("hud.go_o"),{layout_value("hud.go_o_x",144),y},{1,1},tint);
        r.sprite_at(ui_path("hud.go_exclaim"),{layout_value("hud.go_exclaim_x",214),y},{1,1},tint);
    }
}

void draw_battle_results(RenderEngine& r, const BattleHudState& state) {
    const unsigned count=static_cast<unsigned>(state.bodies.size());
    for (unsigned i=0;i<state.bodies.size() && i<4;++i) {
        const float x=results_column_x(i,count);
        r.sprite_at(ui_path("results.arrow_prefix")+std::to_string(i+1)+ui_path("results.arrow_suffix"),{x+17,49});
        r.sprite_at(stock_sprite(state.bodies[i].kind),{x+7,49});
    }
    r.sprite_at(ui_path("results.place"),{10,66});
    r.sprite_at(ui_path("results.kos"),{26,124});
    for (unsigned i=0;i<state.bodies.size() && i<4;++i) {
        const float x=results_column_x(i,count);
        const int place=(state.winner>=0 && static_cast<unsigned>(state.winner)==i)?1:2;
        r.sprite_at(ui_numbered("hud.status_digit_prefix",place),{x+15,66});
        const int kos=i<state.kos.size()?std::min(999,state.kos[i]):0;
        auto digit=[&](int value,float dx,bool hide) {
            r.sprite_at(ui_numbered("hud.status_digit_prefix",value),{x+dx,124},{1,1},
                        hide?Color{255,255,255,0}:Color{255,255,255,255});
        };
        digit(kos/100%10,8,kos<100);
        digit(kos/10%10,16,kos<10);
        digit(kos%10,24,false);
    }
    if (state.winner>=0) {
        const auto& body=state.bodies[static_cast<unsigned>(state.winner)];
        const auto remix=body.custom_model.rfind("remix:",0)==0?std::string_view(body.custom_model).substr(6):std::string_view{};
        if (!remix.empty()) custom_results_name(r,remix,24,178);
        else r.sprite_at(std::string(ui_path("results.name_prefix"))+std::string(fighter_kind_name(body.kind))+".png",{24,178});
        r.sprite_at(ui_path("results.winner"),{24,8});
    }
}

} // namespace sagas
