#pragma once
#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace nmmJucePlugin::libraryProtocol
{
    inline bool command(const std::vector<uint8_t>& m,uint8_t code,size_t size)
    {
        if(m.size()!=size || m[0]!=0xf0 || m[1]!=0x33 || (m[2]&0x7c)!=0x5c || m[3]!=6 || m[4]!=65 || m[5]!=code || m.back()!=0xf7) return false;
        unsigned sum=m[0];for(size_t i=1;i+2<m.size();++i) {if(m[i]&128) return false;sum+=m[i];}
        return (sum&127)==m[m.size()-2];
    }
    inline std::vector<uint8_t> frame(std::vector<uint8_t> payload,uint8_t slot=0)
    {
        std::vector<uint8_t> result{0xf0,0x33,uint8_t(0x58|(slot&3)),6};result.insert(result.end(),payload.begin(),payload.end());
        unsigned sum=0;for(auto b:result) sum+=b;result.push_back(sum&127);result.push_back(0xf7);return result;
    }
    inline std::vector<uint8_t> list(const std::vector<std::string>& names,unsigned position,uint8_t slot)
    {
        // Firmware list encoding: 16/01/03 header, implicit consecutive positions,
        // 02 empty, 03 section/position cursor, and 04 end marker. At most 12 slots
        // per reply keeps even sixteen-character names below the 256-byte limit.
        std::vector<uint8_t> payload{0,19,0,0,22,1,3,0,uint8_t(position)};
        const auto end=std::min(99u,position+12);
        for(unsigned i=position;i<end;++i)
        {
            if(i>=names.size() || names[i].empty()) payload.push_back(2);
            else
            {
                const auto& name=names[i];const auto size=std::min<size_t>(16,name.size());
                for(size_t k=0;k<size;++k) {const auto c=uint8_t(name[k])&127;payload.push_back(c<32?uint8_t(' '):c);}
                if(size<16) payload.push_back(0);
            }
        }
        payload.insert(payload.end(),{3,uint8_t(end==99?1:0),uint8_t(end==99?0:end),4});
        return frame(std::move(payload),slot);
    }
}
