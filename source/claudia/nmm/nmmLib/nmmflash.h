#pragma once
#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace nmm
{
    // AM29F080B byte-wide command decoder. Program/erase completion is immediate;
    // electrical busy timing and erase suspension are not yet modeled.
    class Flash
    {
    public:
        static constexpr unsigned Size=0x100000;
        Flash():m_data(Size,0xff) {}
        uint8_t read(unsigned address) const
        {
            address&=Size-1;
            if(m_id) {switch(address&0xff) {case 0:return 1;case 1:return 0xd5;case 2:return 0;default:return 0xff;}}
            return m_data[address];
        }
        void write(unsigned address,uint8_t value)
        {
            address&=Size-1;const auto commandAddress=address&0x7ff;
            if(m_stage==3) {m_data[address]&=value;m_stage=0;++m_revision;return;}
            if(value==0xf0) {m_stage=0;m_id=false;return;}
            switch(m_stage)
            {
            case 0:m_stage=commandAddress==0x555 && value==0xaa?1:0;break;
            case 1:m_stage=commandAddress==0x2aa && value==0x55?2:0;break;
            case 2:
                m_stage=0;
                if(commandAddress==0x555)
                {
                    if(value==0x90) m_id=true;
                    else if(value==0xa0) {m_id=false;m_stage=3;}
                    else if(value==0x80) m_stage=4;
                }
                break;
            case 4:m_stage=commandAddress==0x555 && value==0xaa?5:0;break;
            case 5:m_stage=commandAddress==0x2aa && value==0x55?6:0;break;
            case 6:
                if(value==0x30) {std::fill_n(m_data.begin()+(address&0xf0000),0x10000,0xff);++m_revision;}
                else if(commandAddress==0x555 && value==0x10) {std::fill(m_data.begin(),m_data.end(),0xff);++m_revision;}
                m_stage=0;m_id=false;break;
            }
        }
        const std::vector<uint8_t>& data() const {return m_data;}
        void restore(const std::vector<uint8_t>& data)
        {
            if(data.size()!=Size) throw std::runtime_error("Invalid flash image size");
            m_data=data;m_id=false;m_stage=0;++m_revision;
        }
        uint64_t revision() const {return m_revision;}
    private:
        std::vector<uint8_t> m_data;
        unsigned m_stage=0;
        bool m_id=false;
        uint64_t m_revision=0;
    };
}
