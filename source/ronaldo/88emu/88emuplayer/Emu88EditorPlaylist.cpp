#include "Emu88Editor.h"
#include "Emu88EditorBindings.h"
#include "Emu88EditorPlaylist.h"
#include "Emu88EditorWindows.h"
#include "juceRmlUi/rmlEventListener.h"
#include "juceRmlUi/rmlHelper.h"
#include "juceRmlUi/rmlMenu.h"
#include "juceUiLib/messageBox.h"
#include "RmlUi/Core/StringUtilities.h"

#include <algorithm>

namespace emu88Player
{
	using namespace editor;

	void Editor::chooseMidiFiles()
	{
		m_playlistChooser = std::make_unique<juce::FileChooser>(
			"Add MIDI files", juce::File{}, "*.mid;*.midi", true);
		const auto flags = juce::FileBrowserComponent::openMode |
		                   juce::FileBrowserComponent::canSelectFiles |
		                   juce::FileBrowserComponent::canSelectMultipleItems;
		const juce::WeakReference<Editor> safeThis(this);
		m_playlistChooser->launchAsync(flags, [safeThis](const juce::FileChooser& _chooser)
		{
			auto* editor = safeThis.get();
			if(!editor)
				return;
			std::vector<std::string> paths;
			for(const auto& file : _chooser.getResults())
				paths.push_back(file.getFullPathName().toStdString());
			editor->addMidiFiles(paths);
			editor->m_playlistChooser.reset();
		});
	}

	void Editor::addMidiFiles(const std::vector<std::string>& _files)
	{
		const auto result = m_processor.midiPlayer().addFiles(_files);
		if(result.errors.empty())
			return;
		std::ostringstream message;
		message << "Some files could not be added:";
		for(const auto& error : result.errors)
			message << "\n\n" << error;
		genericUI::MessageBox::showOk(genericUI::MessageBox::Icon::Warning,
			"88emuPlayer - MIDI playlist", message.str());
	}

	void Editor::attachRecordButton()
	{
		auto* window = dynamic_cast<juce::DocumentWindow*>(getTopLevelComponent());
		if(!window)
			return;

		if(!m_recordButton)
		{
			m_recordButton = std::make_unique<TitleBarButton>(g_recordLabel);
			const juce::WeakReference<Editor> safeThis(this);
			m_recordButton->onClick = [safeThis]
			{
				if(auto* editor = safeThis.get())
					editor->toggleWavRecording();
			};
			updateRecordButton();
		}

		if(m_recordButton->getParentComponent() != window)
			window->juce::Component::addAndMakeVisible(*m_recordButton);

		m_recordButton->applyBounds();
	}

	void Editor::updateRecordButton()
	{
		if(m_recordButton)
			m_recordButton->setRecording(m_processor.isRecording());
	}

	void Editor::toggleWavRecording()
	{
		if(!m_processor.isRecording())
		{
			if(!m_processor.startRecording())
				genericUI::MessageBox::showOk(genericUI::MessageBox::Icon::Warning, "Record to WAV",
					"Recording needs a running audio device. Open Audio/MIDI Settings and select one.", this);
			updateRecordButton();
			return;
		}

		const auto recording = m_processor.stopRecording();
		updateRecordButton();

		if(recording == juce::File())
		{
			genericUI::MessageBox::showOk(genericUI::MessageBox::Icon::Warning, "Record to WAV",
				"Nothing was recorded, so no file was written.", this);
			return;
		}
		saveRecording(recording);
	}

	void Editor::saveRecording(const juce::File& _recording)
	{
		const auto lastFolder = m_processor.config().getValue("lastRecordingFolder");
		auto folder = lastFolder.isNotEmpty() ? juce::File(lastFolder)
		                                      : juce::File::getSpecialLocation(juce::File::userMusicDirectory);
		if(!folder.isDirectory())
			folder = juce::File::getSpecialLocation(juce::File::userHomeDirectory);

		m_recordingChooser = std::make_unique<juce::FileChooser>(
			"Save recording", folder.getChildFile(_recording.getFileName()), "*.wav", true);
		const auto flags = juce::FileBrowserComponent::saveMode |
		                   juce::FileBrowserComponent::canSelectFiles |
		                   juce::FileBrowserComponent::warnAboutOverwriting;
		const juce::WeakReference<Editor> safeThis(this);
		m_recordingChooser->launchAsync(flags, [safeThis, _recording](const juce::FileChooser& _chooser)
		{
			auto* editor = safeThis.get();
			auto target = _chooser.getResult();

			if(target == juce::File())
			{
				_recording.deleteFile();
			}
			else
			{
				if(!target.hasFileExtension("wav"))
					target = target.withFileExtension("wav");

				// moveFileTo cannot rename across volumes, hence the copy fallback.
				const auto saved = _recording.moveFileTo(target) ||
				                   (_recording.copyFileTo(target) && _recording.deleteFile());

				if(editor)
				{
					if(saved)
					{
						editor->m_processor.config().setValue("lastRecordingFolder",
							target.getParentDirectory().getFullPathName());
						editor->m_processor.config().saveIfNeeded();
					}
					else
					{
						genericUI::MessageBox::showOk(genericUI::MessageBox::Icon::Warning, "Record to WAV",
							"The recording could not be written to " + target.getFullPathName().toStdString() +
							"\nIt is still at " + _recording.getFullPathName().toStdString(), editor);
					}
				}
			}

			if(editor)
				editor->m_recordingChooser.reset();
		});
	}

	void Editor::refreshPlaylist()
	{
		if(!m_playlistEntries)
			return;
		m_playlistRows.clear();
		const auto entries = m_processor.midiPlayer().entries();
		std::ostringstream rml;
		if(entries.empty())
			rml << "<div class=\"playlistEmpty\">Drop MIDI files here</div>";
		for(size_t i = 0; i < entries.size(); ++i)
			rml << "<div id=\"playlistEntry" << i << "\" class=\"playlistEntry\">"
			    << "<div class=\"playlistEntryName\">"
			    << Rml::StringUtilities::EncodeRml(entries[i].name) << "</div>"
			    << "<button id=\"playlistRemove" << i
			    << "\" class=\"playlistRemove\" title=\"Remove from playlist\">X</button></div>";
		m_playlistEntries->SetInnerRML(rml.str());

		for(size_t i = 0; i < entries.size(); ++i)
		{
			auto* row = m_playlistEntries->GetOwnerDocument()->GetElementById(
				"playlistEntry" + std::to_string(i));
			if(!row)
				continue;
			juceRmlUi::EventListener::Add(row, Rml::EventId::Click,
				[this, i](Rml::Event&) { m_processor.midiPlayer().play(i); });
			if(auto* remove = m_playlistEntries->GetOwnerDocument()->GetElementById(
				"playlistRemove" + std::to_string(i)))
			{
				juceRmlUi::EventListener::Add(remove, Rml::EventId::Mousedown,
					[this](Rml::Event& _event)
					{
						_event.StopPropagation();
						if(juceRmlUi::helper::isContextMenu(_event))
							openPlaylistContextMenu(_event);
					});
				juceRmlUi::EventListener::Add(remove, Rml::EventId::Click,
					[this, i](Rml::Event& _event)
					{
						_event.StopPropagation();
						m_processor.midiPlayer().remove(i);
					});
			}
			m_playlistRows.push_back(std::make_unique<PlaylistRowDrag>(row, i,
				[this](const size_t _from, const size_t _to) { m_processor.midiPlayer().move(_from, _to); },
				[this](const std::vector<std::string>& _files) { addMidiFiles(_files); }));
		}
		m_playlistRevision = m_processor.midiPlayer().playlistRevision();
		updatePlayerVisuals(m_processor.midiPlayer().status());
	}

	void Editor::updatePlayerVisuals(const MidiPlayer::Status& _status)
	{
		const bool playing = _status.state == MidiPlayer::State::Playing;
		if(m_playerPlayGraphic)
			m_playerPlayGraphic->SetClass("hidden", playing);
		if(m_playerPauseGraphic)
			m_playerPauseGraphic->SetClass("hidden", !playing);
		for(size_t i = 0; i < m_playlistRows.size(); ++i)
			if(auto* element = m_playlistRows[i]->getElement())
				element->SetClass("current", static_cast<int>(i) == _status.currentIndex);
		m_playerStatusRevision = _status.revision;
	}

	void Editor::openPlaylistContextMenu(Rml::Event& _event)
	{
		const auto position = juceRmlUi::helper::getMousePos(_event);
		m_contextMenu = std::make_shared<juceRmlUi::Menu>();
		m_contextMenu->addEntry("Clear Playlist", !m_processor.midiPlayer().entries().empty(), false, [this]
		{
			m_processor.midiPlayer().clear();
		});
		m_contextMenu->open(_event.GetTargetElement(), position, 16);
	}
}
