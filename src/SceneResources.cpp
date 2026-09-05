#include <sagas/SceneResources.hpp>

#include <algorithm>
#include <chrono>
#include <bit>
#include <cstring>
#include <stdexcept>

namespace sagas {
namespace {

constexpr std::size_t header_size = 28;
constexpr std::size_t bundle_size = 12;
constexpr std::size_t resource_size = 60;
constexpr std::size_t segment_size = 36;
constexpr std::size_t cue_size = 20;

std::uint16_t u16(std::span<const std::byte> data, std::size_t at) {
    if (at + 2 > data.size()) throw std::runtime_error("truncated scene manifest");
    return std::to_integer<std::uint16_t>(data[at]) |
           (std::to_integer<std::uint16_t>(data[at + 1]) << 8);
}

std::uint32_t u32(std::span<const std::byte> data, std::size_t at) {
    if (at + 4 > data.size()) throw std::runtime_error("truncated scene manifest");
    return std::to_integer<std::uint32_t>(data[at]) |
           (std::to_integer<std::uint32_t>(data[at + 1]) << 8) |
           (std::to_integer<std::uint32_t>(data[at + 2]) << 16) |
           (std::to_integer<std::uint32_t>(data[at + 3]) << 24);
}

float f32(std::span<const std::byte> data, std::size_t at) {
    return std::bit_cast<float>(u32(data, at));
}

std::string string_at(std::span<const std::byte> strings, std::uint32_t offset) {
    if (offset >= strings.size()) throw std::runtime_error("scene manifest string offset is out of range");
    const auto* begin = reinterpret_cast<const char*>(strings.data() + offset);
    const auto remaining = strings.size() - offset;
    const auto* end = static_cast<const char*>(std::memchr(begin, '\0', remaining));
    if (!end) throw std::runtime_error("scene manifest contains an unterminated string");
    return {begin, end};
}

GeometryLayout layout(std::uint8_t value) {
    switch (value) {
        case 0: return GeometryLayout::Direct;
        case 1: return GeometryLayout::DisplayListLinks;
        case 2: return GeometryLayout::JointPairs;
        default: throw std::runtime_error("scene manifest contains an invalid geometry layout");
    }
}

Model3D::FighterWrapper wrapper(std::uint8_t value) {
    switch (value) {
        case 0: return Model3D::FighterWrapper::None;
        case 1: return Model3D::FighterWrapper::TransN;
        case 2: return Model3D::FighterWrapper::XRotN;
        default: throw std::runtime_error("scene manifest contains an invalid fighter wrapper");
    }
}

Color color(std::uint32_t value) {
    return {static_cast<std::uint8_t>(value), static_cast<std::uint8_t>(value >> 8),
            static_cast<std::uint8_t>(value >> 16), static_cast<std::uint8_t>(value >> 24)};
}

} // namespace

SceneResourceManager::SceneResourceManager(AssetRepository& assets)
    : assets_(assets), archive_(assets), loader_(archive_) {}

SceneResourceManager::~SceneResourceManager() {
    if (pending_.valid()) pending_.wait();
}

void SceneResourceManager::load_manifest(std::string_view logical) {
    if (manifest_ == logical) return;
    clear();
    const auto blob = assets_.blob(logical);
    const std::span<const std::byte> data(*blob);
    if (data.size() < header_size || std::memcmp(data.data(), "SGSC", 4) != 0 || u16(data, 4) != 3)
        throw std::runtime_error("unsupported scene resource manifest: " + std::string(logical));
    const auto bundle_count = u32(data, 8);
    const auto resource_count = u32(data, 12);
    const auto segment_count = u32(data, 16);
    const auto cue_count = u32(data, 20);
    const auto string_bytes = u32(data, 24);
    if (bundle_count > 1024 || resource_count > 65536 || segment_count > 4096 || cue_count > 65536)
        throw std::runtime_error("scene resource manifest exceeds runtime bounds");
    const auto bundle_at = header_size;
    const auto resource_at = bundle_at + static_cast<std::size_t>(bundle_count) * bundle_size;
    const auto segment_at = resource_at + static_cast<std::size_t>(resource_count) * resource_size;
    const auto cue_at = segment_at + static_cast<std::size_t>(segment_count) * segment_size;
    const auto string_at_offset = cue_at + static_cast<std::size_t>(cue_count) * cue_size;
    if (string_at_offset > data.size() || string_bytes > data.size() - string_at_offset)
        throw std::runtime_error("truncated scene resource manifest tables");
    const auto strings = data.subspan(string_at_offset, string_bytes);

    for (std::uint32_t bundle_index = 0; bundle_index < bundle_count; ++bundle_index) {
        const auto at = bundle_at + static_cast<std::size_t>(bundle_index) * bundle_size;
        Bundle bundle;
        bundle.name = string_at(strings, u32(data, at));
        const auto first = u32(data, at + 4);
        const auto count = u32(data, at + 8);
        if (first > resource_count || count > resource_count - first)
            throw std::runtime_error("scene bundle resource range is invalid");
        bundle.resources.reserve(count);
        for (std::uint32_t i = 0; i < count; ++i) {
            const auto item = resource_at + static_cast<std::size_t>(first + i) * resource_size;
            Descriptor descriptor;
            descriptor.key = string_at(strings, u32(data, item));
            descriptor.descriptor = string_at(strings, u32(data, item + 4));
            descriptor.animation = string_at(strings, u32(data, item + 8));
            descriptor.materials = string_at(strings, u32(data, item + 12));
            descriptor.material_animation = string_at(strings, u32(data, item + 16));
            descriptor.dependency = string_at(strings, u32(data, item + 20));
            const auto kind_value = std::to_integer<std::uint8_t>(data[item + 24]);
            if (kind_value > static_cast<std::uint8_t>(Kind::Transition))
                throw std::runtime_error("scene manifest contains an invalid resource kind");
            descriptor.kind = static_cast<Kind>(kind_value);
            descriptor.layout = layout(std::to_integer<std::uint8_t>(data[item + 25]));
            descriptor.wrapper = wrapper(std::to_integer<std::uint8_t>(data[item + 26]));
            const auto flags = std::to_integer<std::uint8_t>(data[item + 27]);
            descriptor.unlit = (flags & 1U) != 0;
            descriptor.emit_spotlight = (flags & 2U) != 0;
            descriptor.animation_file = u32(data, item + 28);
            descriptor.transition_frame = f32(data, item + 32);
            descriptor.material_start = f32(data, item + 36);
            descriptor.position = {f32(data, item + 40), f32(data, item + 44), f32(data, item + 48)};
            descriptor.setup_parts = {u32(data, item + 52), u32(data, item + 56)};
            if (descriptor.key.empty() || owners_.contains(descriptor.key))
                throw std::runtime_error("scene manifest contains an empty or duplicate resource key");
            owners_.emplace(descriptor.key, bundle.name);
            bundle.resources.push_back(std::move(descriptor));
        }
        if (!bundles_.emplace(bundle.name, std::move(bundle)).second)
            throw std::runtime_error("scene manifest contains a duplicate bundle");
    }
    timeline_.reserve(segment_count);
    std::unordered_map<std::string, std::size_t> segment_indices;
    for (std::uint32_t i = 0; i < segment_count; ++i) {
        const auto item = segment_at + static_cast<std::size_t>(i) * segment_size;
        SceneTimelineSegment segment;
        segment.name = string_at(strings, u32(data, item));
        segment.renderer = string_at(strings, u32(data, item + 4));
        segment.argument = string_at(strings, u32(data, item + 8));
        segment.bundle = string_at(strings, u32(data, item + 12));
        segment.duration = u32(data, item + 16);
        segment.preload_lead = u32(data, item + 20);
        segment.scale = {f32(data, item + 24), f32(data, item + 28)};
        segment.color = color(u32(data, item + 32));
        if (segment.name.empty() || segment.duration == 0 || segment_indices.contains(segment.name))
            throw std::runtime_error("scene manifest contains an invalid timeline segment");
        segment_indices.emplace(segment.name, timeline_.size());
        timeline_.push_back(std::move(segment));
    }
    for (std::uint32_t i = 0; i < cue_count; ++i) {
        const auto item = cue_at + static_cast<std::size_t>(i) * cue_size;
        const auto segment_name = string_at(strings, u32(data, item));
        const auto found = segment_indices.find(segment_name);
        if (found == segment_indices.end()) throw std::runtime_error("render cue references an unknown segment");
        timeline_[found->second].cues.push_back({string_at(strings, u32(data, item + 4)),
                                                 string_at(strings, u32(data, item + 8)),
                                                 u32(data, item + 12), color(u32(data, item + 16))});
    }
    for (auto& segment : timeline_)
        std::stable_sort(segment.cues.begin(), segment.cues.end(),
                         [](const auto& a, const auto& b) { return a.order < b.order; });
    manifest_ = logical;
}

Model3D SceneResourceManager::build_model(
    const Descriptor& descriptor, const std::unordered_map<std::string, Model3D>& available) {
    Model3D model;
    if (descriptor.kind == Kind::Model) {
        model = loader_.model(descriptor.descriptor, descriptor.animation, descriptor.layout,
                              descriptor.materials, descriptor.material_animation);
    } else if (descriptor.kind == Kind::DisplayList) {
        model = loader_.display_list(descriptor.descriptor, descriptor.layout,
                                     descriptor.materials, descriptor.material_animation);
        if (!descriptor.animation.empty()) {
            if (const auto animation = archive_.symbol(descriptor.animation)) model.animation[0] = animation;
            else throw std::runtime_error("missing scene animation symbol: " + descriptor.animation);
        }
    } else if (descriptor.kind == Kind::Fighter) {
        model = loader_.fighter_model(descriptor.descriptor, descriptor.layout,
                                      descriptor.setup_parts);
        n64::AnimationDecoder animation(archive_);
        const auto scripts = animation.table({descriptor.animation_file, 0}, model.nodes.size() + (descriptor.wrapper==Model3D::FighterWrapper::None ? 0 : 1));
        if (scripts.empty()) throw std::runtime_error("fighter animation table is empty: " + descriptor.key);
        model.fighter_root.scale = {1, 1, 1};
        model.fighter_root_animation = descriptor.wrapper==Model3D::FighterWrapper::None
            ? std::nullopt : scripts.front();
        model.fighter_wrapper = descriptor.wrapper;
        model.animation.assign(scripts.begin() + (descriptor.wrapper==Model3D::FighterWrapper::None ? 0 : 1), scripts.end());
        model.fighter_animation = true;
        model.is_fighter = true;
    } else {
        const auto previous = available.find(descriptor.dependency);
        if (previous == available.end())
            throw std::runtime_error("scene transition dependency is not loaded: " + descriptor.dependency);
        model = previous->second;
        n64::AnimationDecoder animation(archive_);
        if (model.fighter_wrapper == descriptor.wrapper && model.fighter_root_animation) {
            n64::AnimationDecoder::apply(model.fighter_root, animation.sample16(
                *model.fighter_root_animation, descriptor.transition_frame, animation.pose(model.fighter_root)));
        } else {
            model.fighter_root = {};
            model.fighter_root.scale = {1, 1, 1};
        }
        for (std::size_t i = 0; i < model.nodes.size(); ++i) {
            if (i < model.animation.size() && model.animation[i])
                n64::AnimationDecoder::apply(model.nodes[i], animation.sample16(
                    *model.animation[i], descriptor.transition_frame, animation.pose(model.nodes[i])));
        }
        const auto scripts = animation.table({descriptor.animation_file, 0}, model.nodes.size() + (descriptor.wrapper==Model3D::FighterWrapper::None ? 0 : 1));
        if (scripts.empty()) throw std::runtime_error("fighter transition table is empty: " + descriptor.key);
        model.fighter_root_animation = descriptor.wrapper==Model3D::FighterWrapper::None
            ? std::nullopt : scripts.front();
        model.fighter_wrapper = descriptor.wrapper;
        model.animation.assign(scripts.begin() + (descriptor.wrapper==Model3D::FighterWrapper::None ? 0 : 1), scripts.end());
        model.fighter_animation = true;
        model.is_fighter = true;
    }
    model.receive_lighting = !descriptor.unlit;
    model.emit_spotlight = descriptor.emit_spotlight;
    model.additive = descriptor.emit_spotlight;
    model.material_animation_start = descriptor.material_start;
    model.position = descriptor.position;
    return model;
}

SceneResourceManager::BuiltBundle SceneResourceManager::build(
    const Bundle& bundle, std::unordered_map<std::string, Model3D> dependencies) {
    BuiltBundle built{bundle.name, {}};
    for (const auto& descriptor : bundle.resources) {
        Model3D model;
        try { model=build_model(descriptor,dependencies); }
        catch (const std::exception& error) {
            throw std::runtime_error("scene resource "+descriptor.key+": "+error.what());
        }
        dependencies.insert_or_assign(descriptor.key, model);
        built.models.emplace(descriptor.key, std::move(model));
    }
    return built;
}

void SceneResourceManager::merge(BuiltBundle bundle) {
    for (auto& [key, model] : bundle.models) models_.insert_or_assign(key, std::move(model));
    loaded_bundles_[bundle.name] = true;
}

void SceneResourceManager::poll() {
    if (pending_.valid() && pending_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        merge(pending_.get());
        pending_name_.clear();
    }
}

void SceneResourceManager::prefetch(std::string_view name) {
    poll();
    const std::string key(name);
    if (loaded(key) || pending_.valid()) return;
    const auto found = bundles_.find(key);
    if (found == bundles_.end()) throw std::runtime_error("unknown scene resource bundle: " + key);
    std::unordered_map<std::string, Model3D> dependencies;
    for (const auto& descriptor : found->second.resources) {
        if (descriptor.dependency.empty()) continue;
        if (const auto dependency = models_.find(descriptor.dependency); dependency != models_.end())
            dependencies.emplace(dependency->first, dependency->second);
    }
    const Bundle bundle = found->second;
    pending_name_ = key;
    pending_ = std::async(std::launch::async, [this, bundle, dependencies = std::move(dependencies)]() mutable {
        return build(bundle, std::move(dependencies));
    });
}

void SceneResourceManager::activate(std::string_view name) {
    const std::string key(name);
    if (loaded(key)) return;
    if (pending_.valid()) {
        merge(pending_.get());
        pending_name_.clear();
        if (loaded(key)) return;
    }
    const auto found = bundles_.find(key);
    if (found == bundles_.end()) throw std::runtime_error("unknown scene resource bundle: " + key);
    std::unordered_map<std::string, Model3D> dependencies;
    for (const auto& descriptor : found->second.resources) {
        if (descriptor.dependency.empty()) continue;
        if (const auto dependency = models_.find(descriptor.dependency); dependency != models_.end())
            dependencies.emplace(dependency->first, dependency->second);
    }
    merge(build(found->second, std::move(dependencies)));
}

bool SceneResourceManager::ready(std::string_view name) const {
    if (loaded(name)) return true;
    return pending_.valid() && pending_name_ == name &&
           pending_.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
}

bool SceneResourceManager::loaded(std::string_view name) const {
    const auto found = loaded_bundles_.find(std::string(name));
    return found != loaded_bundles_.end() && found->second;
}

const Model3D& SceneResourceManager::model(std::string_view key) const {
    const auto found = models_.find(std::string(key));
    if (found == models_.end()) throw std::runtime_error("scene resource is not active: " + std::string(key));
    return found->second;
}

void SceneResourceManager::release(std::string_view name) {
    if (pending_.valid() && pending_name_ == name) {
        pending_.wait();
        pending_ = {};
        pending_name_.clear();
    }
    for (auto item = models_.begin(); item != models_.end();) {
        const auto owner = owners_.find(item->first);
        if (owner != owners_.end() && owner->second == name) item = models_.erase(item);
        else ++item;
    }
    loaded_bundles_.erase(std::string(name));
}

void SceneResourceManager::clear() {
    if (pending_.valid()) {
        pending_.wait();
        pending_ = {};
    }
    pending_name_.clear();
    models_.clear();
    bundles_.clear();
    owners_.clear();
    loaded_bundles_.clear();
    timeline_.clear();
    manifest_.clear();
}

} // namespace sagas
