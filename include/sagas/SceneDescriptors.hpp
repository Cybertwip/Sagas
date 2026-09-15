#pragma once
#include <sagas/FighterDescriptors.hpp>
#include <sagas/Fighter.hpp>
#include <stdexcept>
#include <string>
#include <string_view>
namespace sagas {

struct UiSprite { std::string id, path; };
inline void descriptor_read(std::istream& input,UiSprite& value) {
    descriptor_read(input,value.id);descriptor_read(input,value.path);
}
inline const DescriptorTable<UiSprite> ui_sprites{"ui_sprites.tsv","id\tpath",0,DescriptorBank::Scenes};

struct SceneLayoutValue { std::string id; float value{}; };
inline void descriptor_read(std::istream& input,SceneLayoutValue& value) {
    descriptor_read(input,value.id);descriptor_read(input,value.value);
}
inline const DescriptorTable<SceneLayoutValue> scene_layout{"scene_layout.tsv","id\tvalue",0,DescriptorBank::Scenes};

struct CssSlot { unsigned index{}; std::string key; };
inline void descriptor_read(std::istream& input,CssSlot& value) {
    descriptor_read(input,value.index);descriptor_read(input,value.key);
}
inline const DescriptorTable<CssSlot> css_slots{"css_slots.tsv","index\tkey",0,DescriptorBank::Scenes};

struct CssKind {
    std::string key;
    unsigned kind{};
    std::string portrait, card_name, stock_dir;
};
inline void descriptor_read(std::istream& input,CssKind& value) {
    descriptor_read(input,value.key);descriptor_read(input,value.kind);
    descriptor_read(input,value.portrait);descriptor_read(input,value.card_name);descriptor_read(input,value.stock_dir);
}
inline const DescriptorTable<CssKind> css_kinds{"css_kinds.tsv","key\tkind\tportrait\tcard_name\tstock_dir",0,DescriptorBank::Scenes};

struct MenuItemRow {
    std::string screen;
    unsigned index{};
    std::string sprite;
    float tab_x{}, tab_y{}, label_x{}, label_y{};
    std::string action;
};
inline void descriptor_read(std::istream& input,MenuItemRow& value) {
    descriptor_read(input,value.screen);descriptor_read(input,value.index);descriptor_read(input,value.sprite);
    descriptor_read(input,value.tab_x);descriptor_read(input,value.tab_y);
    descriptor_read(input,value.label_x);descriptor_read(input,value.label_y);
    descriptor_read(input,value.action);
}
inline const DescriptorTable<MenuItemRow> menu_items{"menu_items.tsv","screen\tindex\tsprite\ttab_x\ttab_y\tlabel_x\tlabel_y\taction",0,DescriptorBank::Scenes};

struct HdModel {
    std::string key, parent, fbx, model, portrait;
};
inline void descriptor_read(std::istream& input,HdModel& value) {
    descriptor_read(input,value.key);descriptor_read(input,value.parent);
    descriptor_read(input,value.fbx);descriptor_read(input,value.model);descriptor_read(input,value.portrait);
}
inline const DescriptorTable<HdModel> hd_models{"hd_models.tsv","key\tparent\tfbx\tmodel\tportrait",0,DescriptorBank::Scenes};

inline const std::string& ui_path(std::string_view id) {
    for (const auto& row:ui_sprites) if (row.id==id) return row.path;
    throw std::runtime_error("missing ui sprite: "+std::string(id));
}
inline float layout_value(std::string_view id, float fallback=0) {
    for (const auto& row:scene_layout) if (row.id==id) return row.value;
    return fallback;
}
inline const CssKind* css_kind(std::string_view key) {
    for (const auto& row:css_kinds) if (row.key==key) return &row;
    return nullptr;
}
inline std::string_view stock_dir(FighterKind kind) {
    const auto index=static_cast<unsigned>(kind);
    for (const auto& row:css_kinds) if (row.kind==index) return row.stock_dir;
    return "MarioModel";
}

} // namespace sagas
