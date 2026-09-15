#include <sagas/CssRoster.hpp>
#include <sagas/SceneDescriptors.hpp>
#include <sagas/RemixDescriptors.hpp>

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <sstream>
#include <unordered_set>

namespace sagas {

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
    layout.start_1p_x=layout_value("css.1p_start_x",25);
    layout.start_1p_y=layout_value("css.1p_start_y",36);
    layout.logical_1p_w=layout_value("css.1p_logical_w",320);
    layout.logical_1p_h=layout_value("css.1p_logical_h",240);
    layout.card_1p_x0=layout_value("css.1p_card_x0",22);
    layout.card_1p_step=layout_value("css.1p_card_step",69);
    layout.card_1p_y=layout_value("css.1p_card_y",131);
    layout.camera_fov=layout_value("css.camera_fov",30);
    layout.camera_aspect_w=layout_value("css.camera_aspect_w",45);
    layout.camera_aspect_h=layout_value("css.camera_aspect_h",44);
    layout.columns=static_cast<int>(layout_value("css.columns",10));
    layout.visible_rows=static_cast<int>(layout_value("css.visible_rows",3));
    layout.columns_1p=static_cast<int>(layout_value("css.1p_columns",6));
    return layout;
}

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

namespace {
std::string lower_copy(std::string value) {
    for (auto& ch:value) ch=static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return value;
}
std::string upper_copy(std::string value) {
    for (auto& ch:value) ch=static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    return value;
}
bool allowed_portrait(const std::string& portrait) {
    return portrait.find("..")==std::string::npos &&
        (portrait.find("mods/")==0 || portrait.find("css/")==0 || portrait.find("textures/")==0);
}
}

CssRoster CssRoster::build(Services& services, bool one_player) {
    CssRoster roster;
    std::unordered_set<std::string> used;
    const auto push=[&](FighterKind kind,std::string portrait,std::string model,std::string name) {
        if (!portrait.empty() && (!allowed_portrait(portrait) || !services.assets.exists(portrait))) portrait.clear();
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
            if (hd.key.empty()) continue;
            if (!used.insert(hd.key).second) continue;
            FighterKind kind=FighterKind::Mario;
            const auto parent=upper_copy(hd.parent);
            if (const auto* row=css_kind(parent)) kind=static_cast<FighterKind>(row->kind);
            auto model=hd.model=="-"?std::string{}:hd.model;
            const auto slug=lower_copy(hd.key);
            if (model.empty()) {
                for (const auto& guess:{
                    "mods/hd/"+slug+"/converted/model.sgmesh",
                    "mods/hd/"+slug+"/model.sgmesh",
                    "mods/characters/"+slug+"/converted/model.sgmesh"}) {
                    if (services.assets.exists(guess)) {model=guess;break;}
                }
            }
            auto portrait=hd.portrait=="-"?std::string{}:hd.portrait;
            if (portrait.empty()) {
                const auto guess="mods/characters/"+slug+"/source/"+hd.key+".png";
                if (services.assets.exists(guess)) portrait=guess;
                else portrait=css_portrait_path(parent,kind);
            }
            if (model.empty()) continue;
            push(kind,std::move(portrait),std::move(model),hd.key);
        }
        if (services.assets.exists("mods/roster.tsv")) {
            const auto bytes=services.assets.blob("mods/roster.tsv");
            std::istringstream input(std::string(reinterpret_cast<const char*>(bytes->data()),bytes->size()));
            std::string line;std::getline(input,line);
            int seen=0;
            while (std::getline(input,line) && roster.entries.size()<120) {
                std::istringstream fields(line);std::string base,name,portrait,model,cell;
                if (!std::getline(fields,base,'\t') || !std::getline(fields,name,'\t')) continue;
                std::getline(fields,portrait,'\t');std::getline(fields,model,'\t');std::getline(fields,cell,'\t');
                ++seen;if (seen<=12) continue;
                int index=-1;try {index=std::stoi(base);} catch (...) {continue;}
                if (index<0 || index>=12) continue;
                if (!used.insert(name).second) continue;
                push(static_cast<FighterKind>(index),portrait,model,name);
            }
        }
    }
    return roster;
}

} // namespace sagas
