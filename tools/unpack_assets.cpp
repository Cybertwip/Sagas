#include <algorithm>
#include <charconv>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;

namespace {

struct RelocRow {
    bool compressed{};
    std::uint64_t data_offset{}, internal_offset{}, compressed_size{}, external_offset{}, size_words{};
};

std::string trim(std::string value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    const auto last = value.find_last_not_of(" \t\r\n");
    return first == std::string::npos ? std::string{} : value.substr(first, last - first + 1);
}

std::uint64_t number(std::string value) {
    value = trim(std::move(value));
    const int base = value.starts_with("0x") ? 16 : 10;
    if (base == 16) value.erase(0, 2);
    std::uint64_t result{};
    const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), result, base);
    if (error != std::errc{} || end != value.data() + value.size())
        throw std::runtime_error("invalid integer in reloc table: " + value);
    return result;
}

std::vector<RelocRow> read_table(const fs::path& path) {
    std::ifstream input(path);
    if (!input) throw std::runtime_error("cannot open " + path.string());
    std::vector<RelocRow> rows;
    std::string line;
    std::getline(input, line);
    while (std::getline(input, line)) {
        if (trim(line).empty()) continue;
        std::vector<std::string> fields;
        std::stringstream stream(line);
        while (std::getline(stream, line, ',')) fields.push_back(trim(line));
        if (fields.size() != 6) throw std::runtime_error("malformed reloc table row");
        rows.push_back({number(fields[0]) != 0, number(fields[1]), number(fields[2]),
                        number(fields[3]), number(fields[4]), number(fields[5])});
    }
    if (rows.size() < 2) throw std::runtime_error("empty reloc table");
    return rows;
}

std::map<std::size_t, std::string> read_names(const fs::path& path) {
    std::ifstream input(path);
    std::map<std::size_t, std::string> names;
    std::string line;
    while (std::getline(input, line)) {
        if (!line.starts_with('-')) continue;
        const auto colon = line.find(':');
        if (colon == std::string::npos) continue;
        names.emplace(static_cast<std::size_t>(number(line.substr(1, colon - 1))), trim(line.substr(colon + 1)));
    }
    return names;
}

std::vector<std::byte> read_bytes(const fs::path& path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) throw std::runtime_error("cannot open " + path.string());
    const auto size = input.tellg();
    input.seekg(0);
    std::vector<std::byte> bytes(static_cast<std::size_t>(size));
    if (!input.read(reinterpret_cast<char*>(bytes.data()), size))
        throw std::runtime_error("cannot read " + path.string());
    return bytes;
}

std::uint16_t read_be16(const std::vector<std::byte>& bytes, std::size_t at) {
    if (at + 2 > bytes.size()) throw std::runtime_error("relocation link is out of bounds");
    return static_cast<std::uint16_t>((std::to_integer<unsigned>(bytes[at]) << 8) |
                                      std::to_integer<unsigned>(bytes[at + 1]));
}

void write_links(const fs::path& destination, std::size_t file_id, const RelocRow& row,
                 const std::vector<std::byte>& file, const std::vector<std::byte>& archive,
                 std::size_t table_size) {
    std::ofstream links(destination, std::ios::trunc);
    links << "location\ttarget_file\ttarget_offset\n";
    auto walk = [&](std::uint16_t current, auto target_file) {
        std::size_t guard{};
        while (current != 0xffffU) {
            if (++guard > file.size() / 4 + 1) throw std::runtime_error("cyclic relocation chain");
            const auto location = static_cast<std::size_t>(current) * 4;
            const auto next = read_be16(file, location);
            const auto target = static_cast<std::size_t>(read_be16(file, location + 2)) * 4;
            links << location << '\t' << target_file() << '\t' << target << '\n';
            current = next;
        }
    };
    walk(static_cast<std::uint16_t>(row.internal_offset), [=] { return file_id; });

    std::size_t external_index{};
    const auto external_ids = table_size + row.data_offset + row.compressed_size * 4;
    walk(static_cast<std::uint16_t>(row.external_offset), [&] {
        const auto id = read_be16(archive, external_ids + external_index * 2);
        ++external_index;
        return static_cast<std::size_t>(id);
    });
}

void write_symbols(const fs::path& header, const fs::path& destination,
                   const std::map<std::size_t, std::string>& names) {
    std::vector<std::pair<std::string, std::size_t>> prefixes;
    for (const auto& [id, name] : names) prefixes.emplace_back("ll" + name, id);
    std::sort(prefixes.begin(), prefixes.end(), [](const auto& a, const auto& b) {
        return a.first.size() > b.first.size();
    });
    std::ifstream input(header);
    std::ofstream output(destination, std::ios::trunc);
    output << "symbol\tfile\toffset\n";
    std::string line;
    while (std::getline(input, line)) {
        const auto begin = line.find("extern int ll");
        const auto end = line.find(';', begin);
        const auto hex = line.find("// 0x", end);
        if (begin == std::string::npos || end == std::string::npos || hex == std::string::npos) continue;
        const auto symbol = trim(line.substr(begin + 11, end - begin - 11));
        if (symbol.ends_with("FileID")) continue;
        const auto match = std::find_if(prefixes.begin(), prefixes.end(), [&](const auto& item) {
            return symbol.starts_with(item.first);
        });
        if (match == prefixes.end()) continue;
        output << symbol << '\t' << match->second << '\t' << number(line.substr(hex + 3)) << '\n';
    }
}

void link_or_copy(const fs::path& source, const fs::path& destination) {
    fs::create_directories(destination.parent_path());
    std::error_code error;
    if (fs::exists(destination)) {
        if (fs::file_size(destination, error) == fs::file_size(source, error)) return;
        fs::remove(destination, error);
    }
    fs::create_hard_link(source, destination, error);
    if (error) fs::copy_file(source, destination, fs::copy_options::overwrite_existing);
}

std::size_t copy_tree(const fs::path& source, const fs::path& destination,
                      const bool skip_reloc = false) {
    if (!fs::exists(source)) return 0;
    std::size_t count{};
    for (const auto& entry : fs::recursive_directory_iterator(source)) {
        if (!entry.is_regular_file()) continue;
        const auto relative = fs::relative(entry.path(), source);
        if (skip_reloc && (relative.begin()->string() == "relocData" ||
                           relative.filename() == "relocData.bin" ||
                           relative.filename() == "relocData.csv")) continue;
        if (relative.filename() == ".DS_Store") continue;
        link_or_copy(entry.path(), destination / relative);
        ++count;
    }
    return count;
}

} // namespace

int main(int argc, char** argv) try {
    fs::path remix_root, output;
    for (int i = 1; i < argc; ++i) {
        const std::string_view argument = argv[i];
        if (argument == "--remix-root" && i + 1 < argc) remix_root = argv[++i];
        else if (argument == "--output" && i + 1 < argc) output = argv[++i];
        else throw std::runtime_error("usage: sagas_asset_unpack --remix-root PATH --output PATH");
    }
    if (remix_root.empty() || output.empty()) throw std::runtime_error("missing required path");

    const auto game = remix_root / "game";
    const auto extracted = game / "assets/us/relocData";
    const auto rows = read_table(game / "assets/us/relocData.csv");
    const auto names = read_names(game / "tools/relocFileDescriptions.us.txt");
    const auto archive = read_bytes(game / "assets/us/relocData.bin");
    const auto table_size = rows.size() * 12;
    fs::create_directories(output / "reloc");
    std::ofstream manifest(output / "reloc/manifest.tsv", std::ios::trunc);
    manifest << "id\tname\tbytes\tcompressed_in_rom\tinternal_reloc_word\texternal_reloc_word\n";

    for (std::size_t id = 0; id + 1 < rows.size(); ++id) {
        const auto& row = rows[id];
        const auto source = extracted / (std::to_string(id) + (row.compressed ? ".vpk0.bin" : ".bin"));
        if (!fs::exists(source)) throw std::runtime_error("missing decompressed reloc " + source.string());
        const auto expected = row.size_words * 4;
        if (fs::file_size(source) != expected)
            throw std::runtime_error("bad decompressed size for reloc " + std::to_string(id));
        std::ostringstream filename;
        filename << std::setw(4) << std::setfill('0') << id << ".bin";
        link_or_copy(source, output / "reloc" / filename.str());
        const auto bytes = read_bytes(source);
        auto links_name = filename.str();
        links_name.replace(links_name.size() - 3, 3, "links.tsv");
        write_links(output / "reloc" / links_name, id, row, bytes, archive, table_size);
        const auto found = names.find(id);
        manifest << id << '\t' << (found == names.end() ? "unknown" : found->second) << '\t'
                 << expected << '\t' << row.compressed << '\t' << row.internal_offset << '\t'
                 << row.external_offset << '\n';
    }
    write_symbols(game / "include/reloc_data.us.h", output / "reloc/symbols.tsv", names);

    const auto texture_count = copy_tree(game / "relocAssets", output / "textures");
    const auto audio_count = copy_tree(game / "build/us/src/audio", output / "audio");
    const auto native_count = copy_tree(game / "assets/us", output / "native", true);
    std::ofstream summary(output / "manifest.tsv", std::ios::trunc);
    summary << "kind\tfiles\nreloc\t" << rows.size() - 1 << "\ntextures\t" << texture_count
            << "\naudio\t" << audio_count << "\nnative\t" << native_count << '\n';
    std::ofstream(output / ".complete", std::ios::trunc)
        << "Sagas external asset bundle v1\n";
    std::cout << "Unpacked " << rows.size() - 1 << " relocs, " << texture_count
              << " textures, " << audio_count << " audio files, and " << native_count
              << " native assets into " << output << '\n';
    return 0;
} catch (const std::exception& error) {
    std::cerr << "asset unpack failed: " << error.what() << '\n';
    return 1;
}
