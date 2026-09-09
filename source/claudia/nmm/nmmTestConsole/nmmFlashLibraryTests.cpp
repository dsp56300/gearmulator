#include "nmmLib/nmmhardware.h"
#include "nmmLib/nmmpatch.h"
#include <fstream>
#include <iostream>
#include <stdexcept>
static void require(bool v,const char* m) {if(!v) throw std::runtime_error(m);}
static void command(nmm::Hardware& hw,std::initializer_list<uint8_t> payload)
{
    std::vector<uint8_t> message{0xf0,0x33,0x5c,6};message.insert(message.end(),payload);
    unsigned sum=0;for(auto b:message) sum+=b;message.push_back(sum&127);message.push_back(0xf7);
    require(hw.sendEditorMidi(message.data(),message.size()),"send flash command");
    for(unsigned i=0;i<750;++i)
    {
        hw.render(256,1000000);const auto bytes=hw.receiveEditorMidi();
        for(auto b:bytes) std::cout<<std::hex<<unsigned(b)<<' ';
        if(hw.editorIdle() && i>600) break;
    }
    std::cout<<std::dec<<'\n';
}
int main(int argc,char** argv)
{
    try
    {
        require(argc==3,"usage: nmmFlashLibraryTests firmware patch");
        std::ifstream file(argv[2]);auto patch=nmm::Patch::parse(file);patch.name="FlashTest";
        nmm::Hardware hw(argv[1]);hw.boot(30000000);hw.loadPatch(patch,30000000);
        command(hw,{65,11,0,0,98});
        const auto entries=hw.flashPatches();
        std::cout<<"slot99="<<entries[98].name<<" hash="<<entries[98].fingerprint<<'\n';
        require(entries[98].name=="FlashTest","native store visible in directory");
        hw.setPatchParameter(1,4,0,80);command(hw,{65,11,0,0,98});
        require(hw.flashPatches()[98].fingerprint!=entries[98].fingerprint,"same-name overwrite changes content fingerprint");
        command(hw,{65,20,0,0});
        nmm::Hardware restored(argv[1]);restored.restoreFlash(hw.flashImage());restored.boot(30000000);
        restored.loadFlashPatch(98,30000000);
        require(restored.editorPatchName()=="FlashTest","native flash load after reboot");
        restored.setMasterVolume(100);restored.sendMidi(0x90,60,100);const auto audio=restored.render(24000,30000000);
        double sum=0,squares=0;for(auto f:audio) {sum+=f[0];squares+=f[0]*f[0];}
        require(squares/audio.size()-sum*sum/(audio.size()*audio.size())>0.00001,"flash patch generates audio");
        command(restored,{65,12,0,98,0});
        require(!restored.flashPatches()[98].fingerprint,"native delete removes directory entry");
        std::cout<<"PASS native flash store, directory, reboot, load/audio and delete\n";
    }
    catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
