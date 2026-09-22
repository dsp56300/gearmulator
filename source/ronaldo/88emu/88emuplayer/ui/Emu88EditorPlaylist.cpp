#include "88emuplayer/ui/Emu88Editor.h"
#include "88emuplayer/ui/Emu88EditorBindings.h"
#include "88emuplayer/ui/Emu88EditorPlaylist.h"
#include "88emuplayer/ui/Emu88EditorWindows.h"
#include "88emuplayer/app/Emu88LaunchOptions.h"
#include "88emuplayer/app/Emu88Playlist.h"
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
		const auto filter = std::string(jucePlayer::midiFile::fileFilter) + ";" + playlist::fileFilter;
		m_playlistChooser = std::make_unique<juce::FileChooser>(
			"Add MIDI/RCP files or playlists", juce::File{}, filter, true);
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
		// A file added on its own is refused when it cannot be read. A playlist's entries keep their
		// place instead, as they do when the playlist is loaded.
		auto& player = m_processor.midiPlayer();
		const auto previousCount = player.entries().size();
		std::vector<std::string> midiFiles;
		std::vector<std::string> errors;
		bool added = false;
		bool kept = false;
		const auto addPending = [&]
		{
			const auto result = player.addFiles(midiFiles);
			added |= result.added != 0;
			errors.insert(errors.end(), result.errors.begin(), result.errors.end());
			midiFiles.clear();
		};
		for(const auto& file : _files)
		{
			if(!playlist::isSupported(file))
			{
				midiFiles.push_back(file);
				continue;
			}
			addPending();
			std::vector<std::string> paths;
			std::string error;
			if(!playlist::read(juce::File(file), paths, error))
			{
				errors.push_back(std::move(error));
				continue;
			}
			const auto result = player.addFiles(paths, jucePlayer::MidiPlayer::Unreadable::Keep);
			added |= result.added + result.unavailable != 0;
			kept |= result.unavailable != 0;
			errors.insert(errors.end(), result.errors.begin(), result.errors.end());
		}
		addPending();

		if(added)
			saveDefaultPlaylist();
		if(errors.empty())
			return;
		// Adding appends, so the entries this kept are the unavailable ones past the previous end.
		std::vector<std::string> unavailable;
		const auto entries = player.entries();
		for(size_t i = previousCount; i < entries.size(); ++i)
			if(!entries[i].available())
				unavailable.push_back(entries[i].path);
		showFileErrors(kept ? "Some files cannot be read right now. Those from a playlist keep their place and "
		                      "are skipped when playing; click one to try it again."
		                    : "Some files could not be added:",
		               errors, unavailable);
	}

	void Editor::loadPlaylist()
	{
		const auto lastFolder = m_processor.config().getValue("lastPlaylistFolder");
		auto folder = lastFolder.isNotEmpty() ? juce::File(lastFolder)
		                                      : juce::File::getSpecialLocation(juce::File::userMusicDirectory);
		if(!folder.isDirectory())
			folder = juce::File::getSpecialLocation(juce::File::userHomeDirectory);
		m_playlistFileChooser = std::make_unique<juce::FileChooser>(
			"Load MIDI playlist", folder, playlist::fileFilter, true);
		const auto flags = juce::FileBrowserComponent::openMode |
		                   juce::FileBrowserComponent::canSelectFiles;
		const juce::WeakReference<Editor> safeThis(this);
		m_playlistFileChooser->launchAsync(flags, [safeThis](const juce::FileChooser& _chooser)
		{
			auto* editor = safeThis.get();
			if(!editor)
				return;
			const auto file = _chooser.getResult();
			if(file != juce::File())
			{
				std::vector<std::string> paths;
				std::string error;
				if(!playlist::read(file, paths, error))
					genericUI::MessageBox::showOk(genericUI::MessageBox::Icon::Warning,
						"Load MIDI playlist", error, editor);
				else
				{
					const auto result = editor->m_processor.midiPlayer().replaceFiles(paths);
					editor->m_processor.config().setValue("lastPlaylistFolder",
						file.getParentDirectory().getFullPathName());
					editor->m_processor.config().saveIfNeeded();
					editor->saveDefaultPlaylist();
					if(!result.errors.empty())
						editor->showPlaylistNotice();
				}
			}
			editor->m_playlistFileChooser.reset();
		});
	}

	void Editor::savePlaylist()
	{
		const auto lastFolder = m_processor.config().getValue("lastPlaylistFolder");
		auto folder = lastFolder.isNotEmpty() ? juce::File(lastFolder)
		                                      : juce::File::getSpecialLocation(juce::File::userMusicDirectory);
		if(!folder.isDirectory())
			folder = juce::File::getSpecialLocation(juce::File::userHomeDirectory);
		m_playlistFileChooser = std::make_unique<juce::FileChooser>(
			"Save MIDI playlist", folder.getChildFile("playlist.m3u8"), playlist::fileFilter, true);
		const auto flags = juce::FileBrowserComponent::saveMode |
			                   juce::FileBrowserComponent::canSelectFiles |
			                   juce::FileBrowserComponent::warnAboutOverwriting;
		const juce::WeakReference<Editor> safeThis(this);
		m_playlistFileChooser->launchAsync(flags, [safeThis](const juce::FileChooser& _chooser)
		{
			auto* editor = safeThis.get();
			if(!editor)
				return;
			auto file = _chooser.getResult();
			if(file != juce::File())
			{
				if(!file.hasFileExtension("m3u") && !file.hasFileExtension("m3u8"))
					file = file.withFileExtension("m3u8");
				std::string error;
				if(playlist::write(file, editor->m_processor.midiPlayer().entries(), error))
				{
					editor->m_processor.config().setValue("lastPlaylistFolder",
						file.getParentDirectory().getFullPathName());
					editor->m_processor.config().saveIfNeeded();
				}
				else
					genericUI::MessageBox::showOk(genericUI::MessageBox::Icon::Warning,
						"Save MIDI playlist", error, editor);
			}
			editor->m_playlistFileChooser.reset();
		});
	}

	void Editor::playPlaylistEntry(const size_t _index)
	{
		// An unavailable entry's file may be back by now. If not, say why instead of starting the
		// next entry, which is what the player would do with it.
		auto& player = m_processor.midiPlayer();
		if(const auto error = player.reload(_index); !error.empty())
		{
			const auto entries = player.entries();
			showFileErrors("This file cannot be read right now:", {error},
			               _index < entries.size() ? std::vector<std::string>{entries[_index].path}
			                                       : std::vector<std::string>{});
			return;
		}
		player.play(_index);
	}

	void Editor::showPlaylistNotice()
	{
		std::vector<std::string> errors;
		std::vector<std::string> paths;
		for(const auto& entry : m_processor.midiPlayer().entries())
		{
			if(entry.available())
				continue;
			errors.push_back(entry.error);
			paths.push_back(entry.path);
		}
		if(errors.empty())
			return;
		showFileErrors("Some files in the playlist cannot be read right now. They keep their place and are "
		               "skipped when playing; click one to try it again.",
		               errors, paths);
	}

	void Editor::showFileErrors(const std::string& _intro, const std::vector<std::string>& _errors,
	                            const std::vector<std::string>& _paths)
	{
		// A whole playlist on a missing drive would not fit on the screen.
		constexpr size_t maxListed = 8;
		std::ostringstream message;
		message << _intro;
		for(size_t i = 0; i < std::min(_errors.size(), maxListed); ++i)
			message << "\n\n" << _errors[i];
		if(_errors.size() > maxListed)
			message << "\n\n...and " << _errors.size() - maxListed << " more.";
		if(const auto hint = privacySettingsHint(_paths); !hint.empty())
			message << "\n\n" << hint;
		genericUI::MessageBox::showOk(genericUI::MessageBox::Icon::Warning,
			"88emuPlayer - MIDI playlist", message.str(), this);
	}

	void Editor::saveDefaultPlaylist()
	{
		if(!standaloneLaunch)
			return;
		std::string error;
		if(!playlist::write(playlist::defaultFile(), m_processor.midiPlayer().entries(), error))
			genericUI::MessageBox::showOk(genericUI::MessageBox::Icon::Warning,
				"88emuPlayer - MIDI playlist", error + "\n\nThe playlist will not be restored next time.", this);
	}

	void Editor::attachRecordButton()
	{
		auto* window = dynamic_cast<juce::DocumentWindow*>(getTopLevelComponent());
		if(!window)
			return;

		if(window->getTitleBarHeight() != g_titleBarHeight)
		{
			// Attaching the editor can reach here before JUCE has sized the window.
			// Keep title-bar layout from shrinking the content and overwriting its scale.
			const auto contentBounds = window->getContentComponent()->getLocalBounds();
			const juce::ScopedValueSetter<bool> changingScale(m_settingGuiScale, true);
			window->setTitleBarHeight(g_titleBarHeight);
			window->setContentComponentSize(contentBounds.getWidth(), contentBounds.getHeight());
		}

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
			rml << "<div id=\"playlistEntry" << i << "\" class=\"playlistEntry"
			    << (entries[i].available() ? "" : " unavailable") << "\">"
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
				[this, i](Rml::Event&) { playPlaylistEntry(i); });
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
					if(m_processor.midiPlayer().remove(i))
						saveDefaultPlaylist();
				});
			}
			m_playlistRows.push_back(std::make_unique<PlaylistRowDrag>(row, i,
				[this](const size_t _from, const size_t _to)
				{
					if(m_processor.midiPlayer().move(_from, _to))
						saveDefaultPlaylist();
				},
				[this](const std::vector<std::string>& _files) { addMidiFiles(_files); }));
		}
		m_playlistRevision = m_processor.midiPlayer().playlistRevision();
		updatePlayerVisuals(m_processor.midiPlayer().status());
	}

	void Editor::updatePlayerVisuals(const jucePlayer::MidiPlayer::Status& _status)
	{
		const bool playing = _status.state == jucePlayer::MidiPlayer::State::Playing;
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
		m_contextMenu->addEntry("Load Playlist", true, false, [this] { loadPlaylist(); });
		m_contextMenu->addEntry("Save Playlist", !m_processor.midiPlayer().entries().empty(), false,
			[this] { savePlaylist(); });
		m_contextMenu->addEntry("Clear Playlist", !m_processor.midiPlayer().entries().empty(), false, [this]
		{
			if(m_processor.midiPlayer().clear())
				saveDefaultPlaylist();
		});
		m_contextMenu->openPopupWindow(_event.GetTargetElement(), position);
	}
}
