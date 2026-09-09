#pragma once
#include <cstdint>
#include <vector>

namespace nmm
{
    inline bool validEditorPatch(const std::vector<uint8_t>& bytes)
    {
        if(bytes.empty() || bytes.size()>65536) return false;
        size_t begin=0;unsigned sections=0;bool first=true,finished=false;
        while(begin<bytes.size())
        {
            size_t end=begin;while(end<bytes.size() && bytes[end]!=0xf7) ++end;
            if(end==bytes.size() || end-begin<6 || end-begin>255 || finished) return false;
            if(bytes[begin]!=0xf0 || bytes[begin+1]!=0x33 || bytes[begin+3]!=6 || !(bytes[begin+4]&0x40)) return false;
            const auto command=bytes[begin+2];
            if(first ? (command!=0x74 && command!=0x7c) : (command!=0x70 && command!=0x78)) return false;
            finished=command==0x78 || command==0x7c;first=false;
            unsigned sum=bytes[begin];
            for(size_t i=begin+1;i<end-1;++i) {if(bytes[i]&0x80) return false;sum+=bytes[i];}
            if((sum&127)!=bytes[end-1]) return false;
            sections+=bytes[begin+4]&63;begin=end+1;
        }
        return finished && sections==16;
    }
}
