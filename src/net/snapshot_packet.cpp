#include "snapshot_packet.hpp"
#include <enet/enet.h>
#include <algorithm>
#include <array>
#include <memory>
#include <cmath>
#include <set>
#include <stdexcept>
namespace iron::coop {
namespace {
constexpr std::array<uint8_t,4> magic{'I','S','C',1};
constexpr size_t header=9;
using Coder=std::unique_ptr<void,decltype(&enet_range_coder_destroy)>;
Coder coder(){Coder value(enet_range_coder_create(),enet_range_coder_destroy);if(!value)throw std::bad_alloc();return value;}
}
std::vector<uint8_t> encodeSnapshotPacket(const nlohmann::json& j){
    auto raw=nlohmann::json::to_cbor(j);if(raw.empty()||raw.size()>maxStateBytes-header)throw std::length_error("Snapshot exceeds network limit");
    std::vector<uint8_t> bytes(header+raw.size());std::copy(magic.begin(),magic.end(),bytes.begin());
    bytes[4]=0;for(unsigned i=0;i<4;++i)bytes[5+i]=uint8_t(raw.size()>>(24-8*i));
    auto context=coder();ENetBuffer buffer{};buffer.data=raw.data();buffer.dataLength=raw.size();
    size_t packed=enet_range_coder_compress(context.get(),&buffer,1,raw.size(),bytes.data()+header,raw.size());
    if(packed&&packed<raw.size()){bytes[4]=1;bytes.resize(header+packed);}else std::copy(raw.begin(),raw.end(),bytes.begin()+header);
    return bytes;
}
nlohmann::json decodeStatePacket(std::span<const uint8_t> bytes,size_t limit,bool allowFramed){
    if(bytes.empty()||bytes.size()>limit)throw std::length_error("Network state exceeds limit");
    std::vector<uint8_t> expanded;
    if(bytes.size()>=magic.size()&&std::equal(magic.begin(),magic.end(),bytes.begin())){
        if(!allowFramed||bytes.size()<=header)throw std::runtime_error("Invalid snapshot envelope");
        size_t size=0;for(unsigned i=0;i<4;++i)size=(size<<8)|bytes[5+i];
        if(!size||size>limit-header)throw std::length_error("Expanded snapshot exceeds limit");
        if(bytes[4]==1){
            expanded.resize(size);auto context=coder();
            if(enet_range_coder_decompress(context.get(),bytes.data()+header,bytes.size()-header,expanded.data(),size)!=size)throw std::runtime_error("Invalid snapshot compression");
            bytes=expanded;
        }else if(bytes[4]==0){if(bytes.size()-header!=size)throw std::runtime_error("Invalid raw snapshot length");bytes=bytes.subspan(header);}
        else throw std::runtime_error("Unknown snapshot codec");
    }
    // Validate container counts and nesting before DOM allocation.
    struct Guard: nlohmann::json_sax<nlohmann::json>{
        size_t depth=0,nodes=0;std::vector<std::set<string_t>> keys;
        bool node(){return ++nodes<=262144;}
        bool null()override{return node();}bool boolean(bool)override{return node();}
        bool number_integer(number_integer_t)override{return node();}bool number_unsigned(number_unsigned_t)override{return node();}
        bool number_float(number_float_t v,const string_t&)override{return std::isfinite(v)&&node();}
        bool string(string_t&)override{return node();}bool binary(binary_t&)override{return false;}
        bool start_object(size_t n)override{if(n==size_t(-1)||n>262144||++depth>64||!node())return false;keys.emplace_back();return true;}
        bool key(string_t& value)override{return !keys.empty()&&keys.back().insert(value).second&&node();}bool end_object()override{keys.pop_back();--depth;return true;}
        bool start_array(size_t n)override{return n!=size_t(-1)&&n<=262144&&++depth<=64&&node();}
        bool end_array()override{--depth;return true;}
        bool parse_error(size_t,const std::string&,const nlohmann::detail::exception&)override{return false;}
    } guard;
    if(!nlohmann::json::sax_parse(bytes.begin(),bytes.end(),&guard,nlohmann::json::input_format_t::cbor,true))throw std::runtime_error("Invalid bounded network CBOR");
    auto j=nlohmann::json::from_cbor(bytes.begin(),bytes.end());if(!j.is_object())throw std::runtime_error("Invalid network message");return j;
}
}
