#include <sagas/n64/Archive.hpp>

#include <bit>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace sagas::n64 {
namespace {

std::string reloc_name(std::uint32_t file, std::string_view suffix) {
    std::ostringstream name;
    name << "reloc/" << std::setw(4) << std::setfill('0') << file << suffix;
    return name.str();
}

std::vector<std::string> fields(const std::string& line) {
    std::vector<std::string> result;
    std::stringstream stream(line);
    std::string value;
    while (std::getline(stream, value, '\t')) result.push_back(value);
    return result;
}

} // namespace

RelocArchive::RelocArchive(AssetRepository& assets) : assets_(assets) {
    std::ifstream input(assets_.path("reloc/symbols.tsv"));
    std::string line;
    std::getline(input, line);
    while (std::getline(input, line)) {
        const auto item = fields(line);
        if (item.size() == 3)
            symbols_.emplace(item[0], Address{static_cast<std::uint32_t>(std::stoul(item[1])),
                                               static_cast<std::uint32_t>(std::stoul(item[2]))});
    }
}

std::optional<Address> RelocArchive::symbol(std::string_view name) const {
    const std::scoped_lock lock(mutex_);
    if (const auto found = symbols_.find(std::string(name)); found != symbols_.end()) return found->second;
    return {};
}

std::span<const std::byte> RelocArchive::bytes(std::uint32_t file) {
    const std::scoped_lock lock(mutex_);
    if (auto found = files_.find(file); found != files_.end()) return *found->second;
    auto blob = assets_.blob(reloc_name(file, ".bin"));
    files_.emplace(file, blob);
    return *blob;
}

void RelocArchive::load_links(std::uint32_t file) {
    if (links_loaded_.contains(file)) return;
    links_loaded_[file] = true;
    std::ifstream input(assets_.path(reloc_name(file, ".links.tsv")));
    std::string line;
    std::getline(input, line);
    while (std::getline(input, line)) {
        const auto item = fields(line);
        if (item.size() != 3) continue;
        const Address location{file, static_cast<std::uint32_t>(std::stoul(item[0]))};
        links_.emplace(key(location), Address{static_cast<std::uint32_t>(std::stoul(item[1])),
                                              static_cast<std::uint32_t>(std::stoul(item[2]))});
    }
}

std::optional<Address> RelocArchive::resolve(Address pointer_word) {
    const std::scoped_lock lock(mutex_);
    load_links(pointer_word.file);
    if (const auto found = links_.find(key(pointer_word)); found != links_.end()) return found->second;
    return {};
}

std::uint32_t RelocArchive::u32(Address address) {
    const std::scoped_lock lock(mutex_);
    const auto data = bytes(address.file);
    if (address.offset + 4 > data.size()) throw std::out_of_range("N64 u32 read at "+std::to_string(address.file)+":"+std::to_string(address.offset)+" (size "+std::to_string(data.size())+")");
    const auto* p = data.data() + address.offset;
    return (std::to_integer<std::uint32_t>(p[0]) << 24) |
           (std::to_integer<std::uint32_t>(p[1]) << 16) |
           (std::to_integer<std::uint32_t>(p[2]) << 8) | std::to_integer<std::uint32_t>(p[3]);
}

std::int16_t RelocArchive::s16(Address address) {
    const std::scoped_lock lock(mutex_);
    const auto data=bytes(address.file);
    if (address.offset+2>data.size()) throw std::out_of_range("N64 s16 read at "+
        std::to_string(address.file)+":"+std::to_string(address.offset));
    return static_cast<std::int16_t>((std::to_integer<unsigned>(data[address.offset])<<8) |
                                     std::to_integer<unsigned>(data[address.offset+1]));
}
float RelocArchive::f32(Address address) {
    const std::scoped_lock lock(mutex_);
    return std::bit_cast<float>(u32(address));
}

} // namespace sagas::n64
