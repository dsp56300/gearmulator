#pragma once

#include <cstdint>
#include "public.sdk/source/vst2.x/audioeffectx.h"


class VSTAudioEffect : public AudioEffectX
{
public:
					VSTAudioEffect				(audioMasterCallback audioMaster);
					~VSTAudioEffect				() override {}

	// Processing
	void 			processReplacing			(float**  inputs, float**  outputs, VstInt32 sampleFrames) override;
	void 			processDoubleReplacing		(double** inputs, double** outputs, VstInt32 sampleFrames) override {}

	void			setSampleRate				(float sampleRate) override {}

	void 			resume();

	// Parameters
	void			setParameter				(VstInt32 index, float value) override {params[index]=value;}
	float			getParameter				(VstInt32 index) override {return params[index];}
	void 			getParameterDisplay			(VstInt32 index, char* text) override;
	void 			getParameterName			(VstInt32 index, char* label) override;

	// effect
	bool			getEffectName				(char* name) override {vst_strncpy (name, "Virus Filter", kVstMaxEffectNameLen);return true;}
	bool			getProductString			(char* text) override {vst_strncpy (text, "Virus Filter", kVstMaxProductStrLen);return true;}
	bool			getVendorString				(char* text) override {vst_strncpy (text, "The Usual Suspects", kVstMaxVendorStrLen);return true;}
	VstInt32		getVendorVersion			() override {return VERSIONVST; }
	VstPlugCategory	getPlugCategory				() override {return kPlugCategEffect;}

private:
	float params[3],coeffs[5],hist[4];
	float vals[3];
};
