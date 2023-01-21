#include "patchbrowser.h"

#include "pluginEditor.h"

#include "../synthLib/midiToSysex.h"

using namespace juce;

namespace jucePluginEditorLib
{
	static PatchBrowser* s_lastPatchBrowser = nullptr;

	PatchBrowser::PatchBrowser(const Editor& _editor, pluginLib::Controller& _controller, juce::PropertiesFile& _config, const std::initializer_list<ColumnDefinition>& _columns)
		: m_editor(_editor), m_controller(_controller)
		, m_properties(_config)
		, m_fileFilter("*.syx;*.mid;*.midi;*.vstpreset;*.fxb;*.fxp", "*", "Patch Dumps")
		, m_bankList(FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles, File::getSpecialLocation(File::SpecialLocationType::currentApplicationFile), &m_fileFilter, nullptr)
		, m_search("Search Box")
		, m_patchList("Patch Browser")
	{
		for (const auto& column : _columns)
			m_patchList.getHeader().addColumn(column.name, column.id, column.width);

		const auto bankDir = m_properties.getValue("virus_bank_dir", "");

		if (bankDir.isNotEmpty() && File(bankDir).isDirectory())
		{
			m_bankList.setRoot(bankDir);

			s_lastPatchBrowser = this;
			auto callbackPathBrowser = this;

			juce::Timer::callAfterDelay(1000, [&, bankDir, callbackPathBrowser]()
			{
				if(s_lastPatchBrowser != callbackPathBrowser)
					return;

				const auto lastFile = m_properties.getValue("virus_selected_file", "");
				const auto child = File(bankDir).getChildFile(lastFile);
				if(child.existsAsFile())
				{
					m_bankList.setFileName(child.getFileName());
					m_sendOnSelect = false;
					onFileSelected(child);
					m_sendOnSelect = true;
				}
			});
		}

		fitInParent(m_bankList, "ContainerFileSelector");
		fitInParent(m_patchList, "ContainerPatchList");

		m_search.setColour(TextEditor::textColourId, Colours::white);
		m_search.onTextChange = [this]
		{
			refreshPatchList();
		};
		m_search.setTextToShowWhenEmpty("Search...", Colours::grey);

		fitInParent(m_search, "ContainerPatchListSearchBox");

		m_bankList.addListener(this);
		m_patchList.setModel(this);

		m_romBankSelect = _editor.findComponentT<juce::ComboBox>("RomBankSelect", false);
	}

	PatchBrowser::~PatchBrowser()
	{
		if(s_lastPatchBrowser == this)
			s_lastPatchBrowser = nullptr;
	}

	bool PatchBrowser::selectPrevPreset()
	{
		return selectPrevNextPreset(-1);
	}

	bool PatchBrowser::selectNextPreset()
	{
		return selectPrevNextPreset(1);
	}

	uint32_t PatchBrowser::load(PatchList& _result, std::set<std::string>* _dedupeChecksums, const std::vector<std::vector<uint8_t>>& _packets)
	{
		uint32_t count = 0;
		for (const auto& packet : _packets)
		{
			if (load(_result, _dedupeChecksums, packet))
				++count;
		}
		return count;
	}

	bool PatchBrowser::load(PatchList& _result, std::set<std::string>* _dedupeChecksums, const std::vector<uint8_t>& _data)
	{
		auto* patch = createPatch();
		patch->sysex = _data;
		patch->progNumber = static_cast<int>(_result.size());

		if(!initializePatch(*patch))
			return false;

		if (!_dedupeChecksums)
		{
			_result.push_back(std::shared_ptr<Patch>(patch));
		}
		else
		{
			const auto md5 = std::string(getChecksum(*patch).toHexString().toRawUTF8());

			if (_dedupeChecksums->find(md5) == _dedupeChecksums->end())
			{
				_dedupeChecksums->insert(md5);
				_result.push_back(std::shared_ptr<Patch>(patch));
			}
		}

		return true;
	}

	uint32_t PatchBrowser::loadBankFile(PatchList& _result, std::set<std::string>* _dedupeChecksums, const File& file)
	{
		const auto ext = file.getFileExtension().toLowerCase();
		const auto path = file.getParentDirectory().getFullPathName();

		if (ext == ".syx")
		{
			MemoryBlock data;

			if (!file.loadFileAsData(data))
				return 0;

			std::vector<uint8_t> d;
			d.resize(data.getSize());
			memcpy(&d[0], data.getData(), data.getSize());

			std::vector<std::vector<uint8_t>> packets;
			synthLib::MidiToSysex::splitMultipleSysex(packets, d);

			return load(_result, _dedupeChecksums, packets);
		}

		if (ext == ".mid" || ext == ".midi")
		{
			std::vector<uint8_t> data;

			synthLib::MidiToSysex::readFile(data, file.getFullPathName().getCharPointer());

			if (data.empty())
				return 0;

			std::vector<std::vector<uint8_t>> packets;
			synthLib::MidiToSysex::splitMultipleSysex(packets, data);

			return load(_result, _dedupeChecksums, packets);
		}

		std::vector<std::vector<uint8_t>> packets;
		synthLib::MidiToSysex::extractSysexFromFile(packets, file.getFullPathName().toStdString());

		return load(_result, _dedupeChecksums, packets);
	}

	bool PatchBrowser::selectPrevNextPreset(int _dir)
	{
		if(m_filteredPatches.empty())
			return false;

		const auto idx = m_patchList.getSelectedRow();

		if(idx < 0)
			return false;

		const auto newIdx = idx + _dir;

		if(newIdx < 0 || newIdx >= static_cast<int>(m_filteredPatches.size()))
			return false;

		m_patchList.selectRow(newIdx);
		return true;
	}

	void PatchBrowser::fillPatchList(const std::vector<std::shared_ptr<Patch>>& _patches)
	{
		m_patches.clear();

		for (const auto& patch : _patches)
			m_patches.push_back(patch);

		refreshPatchList();
	}

	void PatchBrowser::refreshPatchList()
	{
		const auto searchValue = m_search.getText();
		const auto selectedPatchName = m_properties.getValue("virus_selected_patch", "");

		m_filteredPatches.clear();
		int i=0;
		int selectIndex = -1;

		for (const auto& patch : m_patches)
		{
			if (searchValue.isEmpty() || juce::String(patch->name).containsIgnoreCase(searchValue))
			{
				m_filteredPatches.push_back(patch);

				if(patch->name == selectedPatchName)
					selectIndex = i;

				++i;
			}
		}
		m_patchList.updateContent();
		m_patchList.deselectAllRows();
		m_patchList.repaint();

		if(selectIndex != -1)
			m_patchList.selectRow(selectIndex);
	}

	void PatchBrowser::onFileSelected(const juce::File& file)
	{
		const auto ext = file.getFileExtension().toLowerCase();

		if (file.existsAsFile() && ext == ".syx" || ext == ".midi" || ext == ".mid" || ext == ".fxb" || ext == ".fxp" || ext == ".vstpreset")
		{
			m_properties.setValue("virus_selected_file", file.getFileName());

			std::vector<std::shared_ptr<Patch>> patches;
			loadBankFile(patches, nullptr, file);

			fillPatchList(patches);
		}

		if(m_romBankSelect)
			m_romBankSelect->setSelectedItemIndex(0);
	}

	void PatchBrowser::fitInParent(juce::Component& _component, const std::string& _parentName) const
	{
		auto* parent = m_editor.findComponent(_parentName);

		_component.setTransform(juce::AffineTransform::scale(2.0f));

		const auto& bounds = parent->getBounds();
		const auto w = bounds.getWidth() >> 1;
		const auto h = bounds.getHeight() >> 1;

		_component.setBounds(0,0, w,h);

		parent->addAndMakeVisible(_component);
	}

	void PatchBrowser::fileClicked(const juce::File& file, const juce::MouseEvent& e)
	{
		const auto ext = file.getFileExtension().toLowerCase();
		const auto path = file.getParentDirectory().getFullPathName();
		if (file.isDirectory() && e.mods.isPopupMenu())
		{
			auto p = PopupMenu();
			p.addItem("Add directory contents to patch list", [this, file]()
				{
					m_patches.clear();
					m_checksums.clear();
					std::set<std::string> dedupeChecksums;

					PatchList patches;

					for (const auto& f : RangedDirectoryIterator(file, false, "*.syx;*.mid;*.midi", File::findFiles))
						loadBankFile(patches, &dedupeChecksums, f.getFile());

					fillPatchList(patches);
				});
			p.showMenuAsync(PopupMenu::Options());

			return;
		}
		m_properties.setValue("virus_bank_dir", path);
		onFileSelected(file);
	}

	int PatchBrowser::getNumRows()
	{
		return static_cast<int>(m_filteredPatches.size());
	}

	void PatchBrowser::paintRowBackground(Graphics& g, int rowNumber, int width, int height, bool rowIsSelected)
	{
		const auto alternateColour = m_patchList.getLookAndFeel()
			.findColour(ListBox::backgroundColourId)
			.interpolatedWith(m_patchList.getLookAndFeel().findColour(ListBox::textColourId), 0.03f);
		if (rowIsSelected)
			g.fillAll(Colours::lightblue);
		else if (rowNumber & 1)
			g.fillAll(alternateColour);
	}

	void PatchBrowser::paintCell(Graphics& g, int rowNumber, int columnId, int width, int height, bool rowIsSelected)
	{
		if (rowNumber >= getNumRows())
			return;	// Juce what are you up to?

		g.setColour(rowIsSelected ? Colours::darkblue : m_patchList.getLookAndFeel().findColour(ListBox::textColourId));

		const auto& rowElement = m_filteredPatches[rowNumber];

		//auto text = rowElement.name;
		const String text = getCellText(*rowElement, columnId);

		g.drawText(text, 2, 0, width - 4, height, Justification::centredLeft, true);
		g.setColour(m_patchList.getLookAndFeel().findColour(ListBox::backgroundColourId));
		g.fillRect(width - 1, 0, 1, height);
	}

	void PatchBrowser::selectedRowsChanged(int lastRowSelected)
	{
		if(!m_sendOnSelect)
			return;

		const auto idx = m_patchList.getSelectedRow();

		if (idx == -1)
			return;

		const auto& patch = m_filteredPatches[idx];

		if(!activatePatch(*patch))
			return;

		m_properties.setValue("virus_selected_patch", juce::String(patch->name));
	}

	void PatchBrowser::cellDoubleClicked(int rowNumber, int columnId, const juce::MouseEvent& _mouseEvent)
	{
		if (rowNumber == m_patchList.getSelectedRow())
			selectedRowsChanged(0);
	}


	class PatchBrowserSorter
	{
	public:
		PatchBrowserSorter(PatchBrowser& _browser, const int _attributeToSortBy, const bool _forward) : m_browser(_browser), m_attributeToSort(_attributeToSortBy), m_forward(_forward)
		{
		}

		bool operator()(const std::shared_ptr<Patch>& _a, const std::shared_ptr<Patch>& _b) const
		{
			return (compareElements(*_a, *_b) < 0) == m_forward;
		}

	private:
		int compareElements(const Patch& _a, const Patch& _b) const
		{
			return m_browser.comparePatches(m_attributeToSort, _a, _b);
		}

		PatchBrowser& m_browser;
		const int m_attributeToSort;
		const bool m_forward;
	};

	void PatchBrowser::sortOrderChanged(int newSortColumnId, bool isForwards)
	{
		if (newSortColumnId != 0)
		{
			const PatchBrowserSorter sorter(*this, newSortColumnId, isForwards);
			std::sort(m_filteredPatches.begin(), m_filteredPatches.end(), sorter);
			m_patchList.updateContent();
		}
	}
}
