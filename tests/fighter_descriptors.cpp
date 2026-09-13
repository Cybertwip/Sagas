#include <sagas/FighterDescriptors.hpp>
#include <sagas/Fighter.hpp>
#include <sagas/FighterSourceData.hpp>
#include <cassert>
#include <chrono>
#include <functional>

int main() {
    using namespace sagas;
    const auto original = fighter_descriptor_root().parent_path();
    const auto temporary = std::filesystem::temp_directory_path() /
        ("sagas-descriptors-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(temporary/"fighters");
    const auto write = [&](const std::string& contents) {
        std::ofstream(temporary/"fighters"/"test.tsv") << contents;
    };
    const auto rejects = [](const auto& read) {
        bool failed=false;
        try { read(); } catch(const std::runtime_error&) { failed=true; }
        assert(failed);
    };
    set_fighter_descriptor_root(temporary);
    rejects([] { DescriptorTable<float>{"missing.tsv","value"}.size(); });
    for(const auto* bad : {"SAGAS-DATA\t2\nvalue\n1\n", "SAGAS-DATA\t1\nwrong\n1\n",
                          "SAGAS-DATA\t1\nvalue\nnan\n", "SAGAS-DATA\t1\nvalue\n1 2\n",
                          "SAGAS-DATA\t1\nvalue\n"}) {
        write(bad);
        rejects([] { DescriptorTable<float>{"test.tsv","value"}.size(); });
    }
    write("SAGAS-DATA\t1\nvalue\n-1\n");
    rejects([] { DescriptorTable<unsigned>{"test.tsv","value"}.size(); });
    write("SAGAS-DATA\t1\nvalue\n2.5\n");
    DescriptorTable<float> table{"test.tsv","value"};
    assert(table[0]==2.5f);
    set_fighter_descriptor_root(original);
    const float old_gravity=fighter_attributes(FighterKind::Mario).gravity;
    // Change a real fighter attribute in an alternate asset root: no compilation.
    std::ifstream source(original/"fighters"/"fighter_source_data.tsv");
    std::ofstream changed(temporary/"fighters"/"fighter_source_data.tsv");
    std::string line;
    std::getline(source,line);changed<<line<<'\n';
    std::getline(source,line);changed<<line<<'\n';
    std::vector<std::string> columns;
    std::istringstream header(line);
    while(std::getline(header,line,'\t'))columns.push_back(line);
    unsigned row_index=0;
    while(std::getline(source,line)) {
        if(row_index++==static_cast<unsigned>(FighterKind::Mario)) {
            std::istringstream row(line);std::string field;
            for(std::size_t i=0;i<columns.size();++i) {
                std::getline(row,field,'\t');
                changed<<(i?"\t":"")<<(columns[i]=="gravity"?"9.25":field);
            }
            changed<<'\n';
        } else changed<<line<<'\n';
    }
    changed.close();
    set_fighter_descriptor_root(temporary);
    assert(fighter_attributes(FighterKind::Mario).gravity==9.25f);
    set_fighter_descriptor_root(original);
    assert(fighter_attributes(FighterKind::Mario).gravity==old_gravity);
    std::filesystem::remove_all(temporary);
}
