#include <sagas/CssRoster.hpp>
#include <sagas/SceneDescriptors.hpp>
#include <sagas/RemixDescriptors.hpp>

CssLayout CssLayout::load() {
    CssLayout layout;
    layout.logical_w=layout_value("css.logical_w",640);
    layout.logical_h=layout_value("css.logical_h",360);
    layout.cell=layout_value("css.cell",48);
    layout.start_x=layout_value("css.start_x",80);
    layout.start_y=layout_value("css.start_y",40);
    layout.card_x0=layout_value("css.card_x0",28);
    layout.card_step=layout_value("css.card_step",152);
    layout.card_y=layout_value("css.card_y",200);
    layout.card_w=layout_value("css.card_w",132);
    layout.card_h=layout_value("css.card_h",150);
    layout.camera_z=layout_value("css.camera_z",5000);
    layout.fighter_y=layout_value("css.fighter_y",-850);
    layout.cell_1p_w=layout_value("css.1p_cell_w",45);
    layout.cell_1p_h=layout_value("css.1p_cell_h",43);
    layout.columns=static_cast<int>(layout_value("css.columns",10));
    layout.visible_rows=static_cast<int>(layout_value("css.visible_rows",3));
    return layout;
}
#include <cctype>
#include <iomanip>
#include <sstream>
#include <unordered_set>

namespace sagas {

std::string_view css_card_name(FighterKind kind) {
    const auto index=static_cast<unsigned>(kind);
    for (const auto& row:css_kinds) if (row.kind==index) return row.card_name;
    return ui_path("css.card_gray");
}

std::string css_portrait_path(std::string_view key, FighterKind kind) {
    if (const auto* row=css_kind(key)) return row->portrait;
    for (const auto& entry:remix_css) if (entry.key==key) return entry.portrait;
    if (key=="PURIN" || key=="JIGGLYPUFF") {
        if (const auto* row=css_kind("PURIN")) return row->portrait;
    }
    for (const auto& row:css_kinds) if (row.kind==static_cast<unsigned>(kind)) return row.portrait;
    return {};
}

void draw_name_label(RenderEngine& render,std::string_view name,float x,float y,float width,float height,Color color) {
    const float advance=std::min(height*.72f,width/std::max<std::size_t>(name.size(),1));
    const auto font=std::string(ui_path("css.font"));
    for (unsigned char ch:name) {
        ch=static_cast<unsigned char>(std::toupper(ch));
        if (ch>='A' && ch<='Z') render.sprite_rect(font+std::string(1,static_cast<char>(ch))+".png",x,y,advance,height,color);
        x+=advance;
    }
}

CssRoster CssRoster::build(Services& services, bool one_player) {
    CssRoster roster;
    std::unordered_set<std::string> used;
    const auto push=[&](FighterKind kind,std::string portrait,std::string model,std::string name) {
        if (!portrait.empty() && (portrait.find("..")!=std::string::npos ||
            (portrait.find("mods/")!=0 && portrait.find("css/")!=0 && portrait.find("textures/")!=0) ||
            !services.assets.exists(portrait))) portrait.clear();
        if (!model.empty() && model!="-" && model.rfind("remix:",0)!=0 &&
            (model.find("mods/")!=0 || model.find("..")!=std::string::npos || !services.assets.exists(model)))
            model.clear();
        if (model=="-") model.clear();
        roster.entries.push_back({kind,std::move(portrait),std::move(model),std::move(name)});
    };
    const auto add_remix=[&](const RemixFighter& fighter) {
        if (fighter.key=="RANDOM" || fighter.parent>=static_cast<unsigned>(FighterKind::Count)) return;
        if (!used.insert(fighter.key).second) return;
        std::ostringstream reloc;reloc<<"reloc/"<<std::setw(4)<<std::setfill('0')<<fighter.files[3]<<".bin";
        if (!services.assets.exists(reloc.str())) return;
        auto portrait=css_portrait_path(fighter.key,static_cast<FighterKind>(fighter.parent));
        push(static_cast<FighterKind>(fighter.parent),std::move(portrait),"remix:"+fighter.key,fighter.key);
    };
    if (one_player) {
        for (const auto& row:css_kinds) {
            if (row.key=="JIGGLYPUFF") continue;
            if (!used.insert(row.key).second) continue;
            push(static_cast<FighterKind>(row.kind),row.portrait,"",row.key);
        }
    } else {
        for (const auto& slot:css_slots) {
            if (const auto* kind=css_kind(slot.key)) {
                used.insert(slot.key);
                if (slot.key=="JIGGLYPUFF") used.insert("PURIN");
                push(static_cast<FighterKind>(kind->kind),kind->portrait,"",slot.key);
                continue;
            }
            if (const auto* fighter=remix_fighter(slot.key)) add_remix(*fighter);
        }
        for (const auto& fighter:remix_roster) {
            if (fighter.key.empty()) continue;
            const char prefix=fighter.key[0];
            if (prefix=='J' || prefix=='E' || prefix=='N') continue;
            add_remix(fighter);
        }
        for (const auto& hd:hd_models) {
            if (hd.key.empty() || hd.model.empty() || hd.model=="-") continue;
            if (!used.insert(hd.key).second) continue;
            FighterKind kind=FighterKind::Mario;
            std::string parent=hd.parent;
            for (auto& ch:parent) ch=static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
            if (const auto* row=css_kind(parent)) kind=static_cast<FighterKind>(row->kind);
            auto portrait=hd.portrait=="-"?std::string{}:hd.portrait;
            if (portrait.empty()) portrait=css_portrait_path(parent,kind);
            push(kind,std::move(portrait),hd.model,hd.key);
        }
    }
    return roster;
}

} // namespace sagas
