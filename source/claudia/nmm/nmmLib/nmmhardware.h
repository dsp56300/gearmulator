#pragma once
#include <cstdint>
#include <atomic>
#include <exception>
#include <memory>
#include <ostream>
#include <string>
#include <array>
#include <vector>

namespace nmm
{
	struct Patch;
    class Rom;
    struct Capture;
	class Hardware
	{
	public:
        static constexpr unsigned InputBufferLatency=256;
		explicit Hardware(const std::string& firmware, std::ostream* trace = nullptr);
		~Hardware();
        explicit Hardware(std::shared_ptr<const Rom> firmware,std::ostream* trace=nullptr);
        struct Cancelled final:std::exception {const char* what() const noexcept override {return "Patch load superseded";}};
        void setCancellation(const std::atomic<unsigned>* generation,unsigned expected,const std::atomic<bool>* stop);
        void clearCancellation();
		Hardware(const Hardware&) = delete;
		Hardware& operator=(const Hardware&) = delete;
        // Worker-only scheduler selection. Zero retains instruction-interleaved
        // execution for regression comparison; production uses a bounded window.
        void setDspExecutionWindow(uint32_t frameCount);
        // Worker only; storage must outlive attachment. Never changes JIT execution mode.
        void setCapture(Capture* capture);
        void setJitBlockLimit(unsigned instructions, bool deadlineGraphs=false); // Test override before boot: 16, 32 or 64; optional deadline-checked native graph continuations (16-instruction blocks). DO remains bounded.
        void setDeadlineLinkedJit(bool enabled, bool instructionContinuations=false); // Before boot only.
        // Audio-driven execution; zero retains the MCU-led reference. Before boot only.
        // The plugin uses 64-frame grants with threaded=false and checked linking.
        // The optional DSP thread remains experimental and cannot share capture
        // or diagnostics; cooperative execution supports single-writer capture.
        void setAudioDrivenExecution(uint32_t frames, bool threaded=true);
		void boot(uint64_t instructionBudget);
		// Setup only: executes the compiler and may rebuild JIT caches.
		void initializePatch(uint64_t instructionBudget);
		void loadPatch(const std::string& filename, uint64_t instructionBudget);
		void loadPatch(const Patch& patch, uint64_t instructionBudget);
		// Serialized emulator worker only: bounded firmware parameter adapter.
		void setPatchParameter(uint16_t area, uint16_t module, uint16_t parameter, uint8_t value);
		void setMasterVolume(uint8_t value);
        // Live controls: advance a bounded slice to a quiet main-loop boundary.
        // False means keep the latest control value pending and render normally.
        // Call immediately before each setter, on the hardware worker only.
        bool prepareControlUpdate();
        // Disable only while loading: native control calls still advance hardware,
        // but their startup audio is discarded before host playback begins.
        void setControlAudioCapture(bool enabled);
		void sendMidi(uint8_t status, uint8_t data1, uint8_t data2);
		bool sendEditorMidi(const uint8_t* data, size_t size);
		std::vector<uint8_t> receiveEditorMidi();
		// Worker-only native patch serializers, including editor layout/custom data.
		std::vector<std::vector<uint8_t>> exportEditorPatch(uint32_t chunkBytes=166);
		void restoreEditorPatch(const std::vector<uint8_t>& messages);
		bool editorIdle() const;
        bool editorSnapshotReady() const;
		struct EditorKnob {uint8_t area=0,module=0,parameter=0,value=0;};
		std::array<EditorKnob,3> editorKnobs() const;
		EditorKnob editorButton() const;
		std::string editorPatchName() const;
		void notifyEditorPatchChanged();
		// Bring-up API: allocates output and can lazily compile JIT blocks.
		// Use renderInto on the worker; neither API belongs in the audio callback.
		std::vector<std::array<float,2>> render(uint32_t frames, uint64_t instructionBudget);
		// Worker-only, caller-owned buffers; input is optional interleaved stereo.
		void renderInto(std::array<float,2>* output, uint32_t frames, uint64_t instructionBudget, const std::array<float,2>* input=nullptr);
		struct FlashPatch {std::string name;uint64_t fingerprint=0;};
        std::array<FlashPatch,99> flashPatches() const;
        void loadFlashPatch(unsigned position,uint64_t instructionBudget);
        const std::vector<uint8_t>& flashImage() const;
        uint64_t flashRevision() const;
		void restoreFlash(const std::vector<uint8_t>& image);
		void report(std::ostream& output) const;
		// Offline diagnostics only; never call from an audio callback.
		void dumpState(std::ostream& output) const;
        // Quiescent worker only: enumerate existing native JIT blocks without attaching a debugger.
        void dumpJitMap(std::ostream& output, std::ostream* nativeAssembly=nullptr) const;
		struct Timing {uint64_t cycles,frames;uint32_t controlSampleCounter;
            uint64_t maxBlockCycles=0,maxIrqAcceptanceCycles=0,irqRequests=0,irqObserved=0,maxPendingIrqs=0;
            uint64_t maxPeripheralBoundaryOverrun=0; // Instruction-clock ticks, diagnostic returns only.
            uint64_t backpressureYields=0;
            uint32_t maxQueuedAudio=0,maxControlBacklog=0;};
        void enableRuntimeDiagnostics(std::ostream* coefficientTrace=nullptr);
        // Worker-only opt-in compile-time counter; does not attach a debugger.
        void enableCompilationDiagnostics();
        uint64_t compilationCount() const;
        // Offline bounded snapshots at JIT returns and peripheral polls. No stream I/O while rendering.
        void enableDmaBoundaryTrace(size_t capacity=32768);
        void writeDmaBoundaryTrace(std::ostream& out) const;
		Timing timing() const;
		uint32_t readMemory(char space, uint32_t address) const;
	private:
		struct Impl;
		std::unique_ptr<Impl> m_impl;
	};
}
