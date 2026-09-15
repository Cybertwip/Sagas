#pragma once
#include <array>
#include <charconv>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

namespace sagas {
// Set before creating scenes. Editing data files takes effect on the next run.
void set_fighter_descriptor_root(const std::filesystem::path& asset_root);
const std::filesystem::path& fighter_descriptor_root();
const std::filesystem::path& scene_descriptor_root();
unsigned fighter_descriptor_generation();
enum class DescriptorBank { Fighters, Scenes };

template<class T> requires std::is_arithmetic_v<T>
void descriptor_read(std::istream& input,T& value) {
    std::string token;
    if(!(input>>token))throw std::runtime_error("missing numeric field");
    const auto result=std::from_chars(token.data(),token.data()+token.size(),value);
    if(result.ec!=std::errc{} || result.ptr!=token.data()+token.size())throw std::runtime_error("invalid numeric field: "+token);
    if constexpr(std::is_floating_point_v<T>)if(!std::isfinite(value))throw std::runtime_error("non-finite field");
}
inline void descriptor_read(std::istream& input,std::string& value) {
    if(!(input>>value))throw std::runtime_error("missing identifier field");
}
template<class T,std::size_t N> void descriptor_read(std::istream& input,std::array<T,N>& values) {
    for(auto& value:values)descriptor_read(input,value);
}

template<class T> class DescriptorTable {
public:
    constexpr DescriptorTable(const char* file,const char* columns,std::size_t expected_rows=0,
                              DescriptorBank bank=DescriptorBank::Fighters)
        :file_(file),columns_(columns),expected_rows_(expected_rows),bank_(bank) {}
    const T& operator[](std::size_t index) const {return rows().at(index);}
    const T& at(std::size_t index) const {return rows().at(index);}
    auto begin() const {return rows().begin();}
    auto end() const {return rows().end();}
    auto size() const {return rows().size();}
    auto empty() const {return rows().empty();}
    const T* data() const {return rows().data();}
private:
    const std::vector<T>& rows() const {
        if(generation_==fighter_descriptor_generation())return values_;
        const auto path=(bank_==DescriptorBank::Scenes?scene_descriptor_root():fighter_descriptor_root())/file_;
        std::ifstream input(path);
        if(!input)throw std::runtime_error("Missing descriptor: "+path.string());
        std::string line;
        if(!std::getline(input,line) || line!="SAGAS-DATA\t1")throw std::runtime_error("Unsupported descriptor version: "+path.string());
        if(!std::getline(input,line) || line!=columns_)throw std::runtime_error("Descriptor schema mismatch: "+path.string());
        std::vector<T> parsed;unsigned number=2;
        while(std::getline(input,line)) {
            ++number;if(line.empty() || line[0]=='#')continue;
            try {
                std::istringstream row(line);T value{};descriptor_read(row,value);
                row>>std::ws;if(!row.eof())throw std::runtime_error("extra fields");
                parsed.push_back(std::move(value));
            } catch(const std::exception& error) {
                throw std::runtime_error(path.string()+":"+std::to_string(number)+": "+error.what());
            }
        }
        if(parsed.empty())throw std::runtime_error("Empty descriptor: "+path.string());
        if(expected_rows_ && parsed.size()!=expected_rows_)throw std::runtime_error("Descriptor row count mismatch: "+path.string());
        values_=std::move(parsed);generation_=fighter_descriptor_generation();return values_;
    }
    const char* file_;const char* columns_;std::size_t expected_rows_;
    DescriptorBank bank_{};
    mutable unsigned generation_{};
    mutable std::vector<T> values_;
};
template<class T> class DescriptorValue {
public:
    constexpr explicit DescriptorValue(const char* file):table_(file,"value",1) {}
    operator T() const {return table_[0];}
private:
    DescriptorTable<T> table_;
};
}
