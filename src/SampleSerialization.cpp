#include "SampleSerialization.h"
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace zyg {
namespace {
constexpr char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
constexpr char encoding[] = "planar-f32le-base64-v1";
bool validRate(double rate) { return std::isfinite(rate) && rate > 0 && rate <= 768000; }
bool validFrames(std::size_t frames) { return frames >= 2 && frames <= maxEmbeddedSampleFrames; }
std::size_t encodedSize(std::size_t frames) { return ((frames * 4 + 2) / 3) * 4; }
int digit(char ch) {
    if (ch >= 'A' && ch <= 'Z') return ch - 'A';
    if (ch >= 'a' && ch <= 'z') return ch - 'a' + 26;
    if (ch >= '0' && ch <= '9') return ch - '0' + 52;
    return ch == '+' ? 62 : ch == '/' ? 63 : -1;
}
[[noreturn]] void invalid(const char* reason) { throw std::runtime_error(std::string("invalid embedded sample: ") + reason); }
std::string encode(const std::vector<float>& samples) {
    std::string out; out.reserve(encodedSize(samples.size()));
    std::uint32_t bits=0; int count=0;
    for(float value : samples) {
        if(!std::isfinite(value)) invalid("nonfinite audio");
        std::uint32_t word;std::memcpy(&word,&value,4);
        for(int b=0;b<4;++b) {
            bits=(bits<<8)|((word>>(8*b))&255);count+=8;
            while(count>=6) {count-=6;out+=alphabet[(bits>>count)&63];}
        }
    }
    if(count) out+=alphabet[(bits<<(6-count))&63];
    while(out.size()%4) out+='=';
    return out;
}
std::vector<float> decode(const Json& value, std::size_t frames) {
    if(!value.is_string()) invalid("channel must be base64 text");
    const auto& text=value.get_ref<const std::string&>();
    if(text.size()!=encodedSize(frames)) invalid("channel byte count disagrees with frame count");
    const std::size_t bytes=frames*4,characters=(bytes*8+5)/6;
    for(std::size_t i=0;i<text.size();++i) {
        if(i<characters ? digit(text[i])<0 : text[i]!='=') invalid("malformed base64");
    }
    const auto paddingBits=characters*6-bytes*8;
    if(paddingBits && (digit(text[characters-1])&((1u<<paddingBits)-1))) invalid("noncanonical base64 padding");
    std::vector<float> out(frames);
    std::uint32_t bits=0,word=0;int count=0;std::size_t decoded=0;
    for(std::size_t i=0;i<characters;++i) {
        bits=(bits<<6)|std::uint32_t(digit(text[i]));count+=6;
        if(count>=8) {
            count-=8;word|=((bits>>count)&255)<<(8*(decoded%4));
            if(++decoded%4==0) {
                float sample;std::memcpy(&sample,&word,4);word=0;
                if(!std::isfinite(sample)) invalid("nonfinite audio");
                out[decoded/4-1]=sample;
            }
        }
    }
    return out;
}
}

SamplePtr decodePlanarSample(const Json& channels, double rate, int root, std::string& error) {
    error.clear();
    if(!validRate(rate)) {error="invalid embedded sample rate";return {};}
    if(!channels.is_array() || channels.empty() || channels.size()>2) {error="embedded audio requires one or two planar channels";return {};}
    if(!channels[0].is_array() || !validFrames(channels[0].size())) {error="embedded channel length must be 2..4194304 frames";return {};}
    const auto frames=channels[0].size();
    for(const auto& channel:channels) {
        if(!channel.is_array() || channel.size()!=frames) {error="embedded channel lengths differ";return {};}
        for(const auto& value:channel) {
            if(!value.is_number()) {error="embedded audio contains a nonnumeric sample";return {};}
            const double sample=value.get<double>();
            if(!std::isfinite(sample) || std::abs(sample)>std::numeric_limits<float>::max()) {error="embedded audio contains a nonfinite or overflowing sample";return {};}
        }
    }
    auto out=std::make_shared<SampleData>();out->sampleRate=rate;out->rootNote=root;
    out->left.resize(frames);if(channels.size()==2)out->right.resize(frames);
    for(std::size_t i=0;i<frames;++i) {
        out->left[i]=channels[0][i].get<float>();
        if(channels.size()==2)out->right[i]=channels[1][i].get<float>();
    }
    return out;
}

Json embeddedSampleToJson(const SampleData& sample) {
    if(!validRate(sample.sampleRate) || !validFrames(sample.frames()) ||
       (sample.stereo() && sample.right.size()!=sample.frames())) invalid("sample rate or channel length");
    if(sample.rootNote<0 || sample.rootNote>127) invalid("root note");
    Json channels=Json::array({encode(sample.left)});
    if(sample.stereo())channels.push_back(encode(sample.right));
    return {{"encoding",encoding},{"frames",sample.frames()},{"sampleRate",sample.sampleRate},
            {"rootNote",sample.rootNote},{"channels",std::move(channels)}};
}
SamplePtr embeddedSampleFromJson(const Json& json) {
    if(!json.is_object() || json.value("encoding",std::string{})!=encoding) invalid("unknown encoding");
    const auto& count=json.at("frames");
    if(!count.is_number_integer() || count.get<double>()<2 || count.get<double>()>double(maxEmbeddedSampleFrames)) invalid("frame count out of bounds");
    const auto frames=count.get<std::size_t>();
    const auto& rate=json.at("sampleRate");
    if(!rate.is_number() || !validRate(rate.get<double>())) invalid("sample rate");
    const auto& channels=json.at("channels");
    if(!channels.is_array() || channels.empty() || channels.size()>2) invalid("channel count");
    if(json.contains("rootNote") && (!json["rootNote"].is_number_integer() || json["rootNote"].get<double>()<0 || json["rootNote"].get<double>()>127)) invalid("root note");
    auto out=std::make_shared<SampleData>();out->sampleRate=rate.get<double>();out->rootNote=json.value("rootNote",60);
    out->left=decode(channels[0],frames);
    if(channels.size()==2)out->right=decode(channels[1],frames);
    return out;
}
}
