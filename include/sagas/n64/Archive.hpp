#pragma once

#include <sagas/Core.hpp>

#include <cstdint>
#include <optional>
#include <mutex>
#include <span>
#include <string_view>
#include <unordered_map>

namespace sagas::n64 {

struct Address {
    std::uint32_t file{};
    std::uint32_t offset{};
    friend constexpr bool operator==(Address, Address) = default;
};

// Repository + Flyweight: one disk blob and relocation map per referenced file.
class RelocArchive final {
public:
    explicit RelocArchive(AssetRepository& assets);
    [[nodiscard]] std::optional<Address> symbol(std::string_view name) const;
    [[nodiscard]] std::optional<Address> resolve(Address pointer_word);
    [[nodiscard]] std::span<const std::byte> bytes(std::uint32_t file);
    [[nodiscard]] std::uint32_t u32(Address address);
    [[nodiscard]] std::int16_t s16(Address address);
    [[nodiscard]] float f32(Address address);
private:
    void load_links(std::uint32_t file);
    static std::uint64_t key(Address address) {
        return (static_cast<std::uint64_t>(address.file) << 32) | address.offset;
    }
    AssetRepository& assets_;
    mutable std::recursive_mutex mutex_;
    std::unordered_map<std::string, Address> symbols_;
    std::unordered_map<std::uint32_t, std::shared_ptr<const std::vector<std::byte>>> files_;
    std::unordered_map<std::uint64_t, Address> links_;
    std::unordered_map<std::uint32_t, bool> links_loaded_;
};

} // namespace sagas::n64
