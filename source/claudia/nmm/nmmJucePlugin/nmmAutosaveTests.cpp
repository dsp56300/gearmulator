#include "nmmAutosave.h"

#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace
{
    void require(const bool condition,const char* message)
    {
        if(!condition) throw std::runtime_error(message);
    }
}

int main()
{
    try
    {
        auto directory=juce::File("/private/tmp")
            .getChildFile("nmm-autosave-test-"+juce::String::toHexString(juce::Time::getMillisecondCounter()));
        directory.deleteRecursively();
        require(directory.createDirectory().wasOk(),"Cannot create temporary autosave directory");

        auto source=std::make_shared<nmmJucePlugin::PanelState>();
        source->bank={{"First","patch-one",{}},{"Second","patch-two",{}}};
        source->selectedPatch=1;source->patchText="patch-two";source->patchName="Second";
        source->flash.resize(0x100000,0xff);source->flash[0]=0x42;source->flash.back()=0xa5;
        {
            nmmJucePlugin::Autosave autosave(source,directory);
            source->setValue(1,111);
            source->selectPatch(0,true);
            source->requestPatch("unsaved-draft","Draft");
            bool saved=false;
            for(unsigned i=0;i<30 && !saved;++i)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                saved=directory.getChildFile("nmm-recovery.state").existsAsFile();
            }
            require(saved,"Autosave did not produce a recovery file");
        }

        require(!directory.getChildFile("nmm-recovery.active").existsAsFile(),"Clean autosave left a recovery marker");
        require(directory.getChildFile("nmm-recovery.active").replaceWithText("simulated crashed session"),"Cannot simulate recovery marker");
        auto restored=std::make_shared<nmmJucePlugin::PanelState>();
        nmmJucePlugin::Autosave recovery(restored,directory);
        require(recovery.recovered(),"Valid recovery file was not loaded");
        {
            std::lock_guard<std::mutex> lock(restored->mutex);
            require(restored->bank.size()==2,"Recovered bank size mismatch");
            require(restored->bank[0].text=="patch-one","Recovered patch text mismatch");
            require(restored->selectedPatch==0,"Recovered selected patch mismatch");
            require(restored->patchText=="unsaved-draft" && restored->patchName=="Draft" && !restored->patchFromBank,"Recovery lost independent working patch");
            require(restored->bank[0].name=="First" && restored->bank[1].text=="patch-two","Recovery overwrote bank with working patch");
            require(restored->flash.size()==0x100000 && restored->flash[0]==0x42 && restored->flash.back()==0xa5,"Recovered flash mismatch");
        }
        require(restored->values[1]==111,"Recovered parameter mismatch");

        const auto file=directory.getChildFile("nmm-recovery.state");
        require(file.replaceWithText("NMRK damaged"),"Cannot damage recovery file");
        auto damaged=std::make_shared<nmmJucePlugin::PanelState>();
        nmmJucePlugin::Autosave rejected(damaged,directory);
        require(!rejected.recovered(),"Damaged recovery file was accepted");

        directory.deleteRecursively();
        std::cout<<"PASS autosave round trip, flash preservation and corruption rejection\n";
        return 0;
    }
    catch(const std::exception& e)
    {
        std::cerr<<"FAIL "<<e.what()<<'\n';
        return 1;
    }
}
