#include "midiLearnPreset.h"

#include "synthLib/midiTypes.h"

#include <juce_core/juce_core.h>
#include <algorithm>

namespace pluginLib
{
	MidiLearnPreset::MidiLearnPreset(std::string _name)
		: m_name(std::move(_name))
	{
	}

	void MidiLearnPreset::addMapping(const MidiLearnMapping& _mapping)
	{
		m_mappings.push_back(_mapping);
	}

	void MidiLearnPreset::removeMapping(size_t _index)
	{
		if (_index < m_mappings.size())
			m_mappings.erase(m_mappings.begin() + static_cast<ptrdiff_t>(_index));
	}

	void MidiLearnPreset::clearMappings()
	{
		m_mappings.clear();
	}

	void MidiLearnPreset::addInputBlock(const MidiInputBlock& _block)
	{
		if (_block.part == MidiLearnMapping::AutoPart)
		{
			m_inputBlocks.erase(std::remove_if(m_inputBlocks.begin(), m_inputBlocks.end(), [&](const MidiInputBlock& _existing)
			{
				return _existing.paramName == _block.paramName;
			}), m_inputBlocks.end());
		}
		else if (isInputBlocked(_block.paramName, _block.part))
		{
			return;
		}

		m_inputBlocks.push_back(_block);
	}

	void MidiLearnPreset::removeInputBlock(const std::string& _paramName, uint8_t _part)
	{
		m_inputBlocks.erase(std::remove_if(m_inputBlocks.begin(), m_inputBlocks.end(),
			[&](const MidiInputBlock& _block)
			{
				return _block.paramName == _paramName &&
					(_block.part == _part || _block.part == MidiLearnMapping::AutoPart);
			}), m_inputBlocks.end());
	}

	bool MidiLearnPreset::isInputBlocked(const std::string& _paramName, uint8_t _part) const
	{
		return std::any_of(m_inputBlocks.begin(), m_inputBlocks.end(), [&](const MidiInputBlock& _block)
		{
			return _block.paramName == _paramName &&
				(_block.part == MidiLearnMapping::AutoPart || _block.part == _part);
		});
	}

	const MidiLearnMapping* MidiLearnPreset::findMapping(MidiLearnMapping::Type _type, uint8_t _channel, uint8_t _controller) const
	{
		for (const auto& mapping : m_mappings)
		{
			if (mapping.matchesMidiEvent(_type, _channel, _controller))
				return &mapping;
		}
		return nullptr;
	}

	const MidiLearnMapping* MidiLearnPreset::findMapping(const synthLib::SMidiEvent& _event) const
	{
		for (const auto& mapping : m_mappings)
		{
			if (mapping.matchesMidiEvent(_event))
				return &mapping;
		}
		return nullptr;
	}

	std::vector<const MidiLearnMapping*> MidiLearnPreset::findMappingsByParam(const std::string& _paramName) const
	{
		std::vector<const MidiLearnMapping*> result;
		for (const auto& mapping : m_mappings)
		{
			if (mapping.paramName == _paramName)
				result.push_back(&mapping);
		}
		return result;
	}

	juce::var MidiLearnPreset::toJson() const
	{
		auto* obj = new juce::DynamicObject();

		obj->setProperty("name", juce::String(m_name));
		obj->setProperty("version", 1);

		juce::Array<juce::var> mappingsArray;
		for (const auto& mapping : m_mappings)
		{
			mappingsArray.add(mapping.toJson());
		}
		obj->setProperty("mappings", mappingsArray);

		juce::Array<juce::var> inputBlocksArray;
		for (const auto& block : m_inputBlocks)
		{
			auto* blockObj = new juce::DynamicObject();
			blockObj->setProperty("paramName", juce::String(block.paramName));
			blockObj->setProperty("part", static_cast<int>(block.part));
			inputBlocksArray.add(juce::var(blockObj));
		}
		obj->setProperty("inputBlocks", inputBlocksArray);
		obj->setProperty("defaultFeedbackTargets", static_cast<int>(m_defaultFeedbackTargets));

		return juce::var(obj);
	}

	bool MidiLearnPreset::fromJson(const juce::var& _json)
	{
		auto* obj = _json.getDynamicObject();
		if (!obj)
			return false;

		m_name = obj->getProperty("name").toString().toStdString();

		const auto version = static_cast<int>(obj->getProperty("version"));
		if (version != 1)
			return false; // Unsupported version

		m_mappings.clear();
		m_inputBlocks.clear();

		const auto* mappingsArray = obj->getProperty("mappings").getArray();
		if (mappingsArray)
		{
			for (const auto& mappingVar : *mappingsArray)
			{
				auto mapping = MidiLearnMapping::fromJson(mappingVar);
				if (auto* mappingObj = mappingVar.getDynamicObject())
				{
					const bool discarded = mappingObj->hasProperty("discardInput") && static_cast<bool>(mappingObj->getProperty("discardInput"));
					const bool firmwareDefault = mappingObj->hasProperty("defaultController") && static_cast<bool>(mappingObj->getProperty("defaultController"));
					if (discarded)
						addInputBlock({mapping.paramName, mapping.part});
					if (firmwareDefault)
						continue;
				}
				m_mappings.push_back(mapping);
			}
		}

		if (const auto* inputBlocksArray = obj->getProperty("inputBlocks").getArray())
		{
			for (const auto& blockVar : *inputBlocksArray)
			{
				if (auto* blockObj = blockVar.getDynamicObject())
				{
					const auto paramName = blockObj->getProperty("paramName").toString().toStdString();
					const auto part = blockObj->hasProperty("part")
						? static_cast<uint8_t>(static_cast<int>(blockObj->getProperty("part")))
						: MidiLearnMapping::AutoPart;
					addInputBlock({paramName, part});
				}
			}
		}

		if (obj->hasProperty("defaultFeedbackTargets"))
			m_defaultFeedbackTargets = static_cast<uint8_t>(static_cast<int>(obj->getProperty("defaultFeedbackTargets")));

		return true;
	}

	bool MidiLearnPreset::saveToFile(const juce::File& _file) const
	{
		const auto json = toJson();
		const auto jsonString = juce::JSON::toString(json, false); // true = all on one line, but we want it pretty-printed

		return _file.replaceWithText(jsonString);
	}

	bool MidiLearnPreset::loadFromFile(const juce::File& _file)
	{
		if (!_file.existsAsFile())
			return false;

		const auto jsonString = _file.loadFileAsString();
		const auto json = juce::JSON::parse(jsonString);

		return fromJson(json);
	}

	bool MidiLearnPreset::operator==(const MidiLearnPreset& _other) const
	{
		// Compare mappings only, not names
		if (m_mappings.size() != _other.m_mappings.size() || m_inputBlocks != _other.m_inputBlocks)
			return false;

		for (size_t i = 0; i < m_mappings.size(); ++i)
		{
			const auto& a = m_mappings[i];
			const auto& b = _other.m_mappings[i];

			if (a.type != b.type ||
			    a.channel != b.channel ||
			    a.part != b.part ||
			    a.controller != b.controller ||
			    a.paramName != b.paramName ||
			    a.mode != b.mode ||
			    a.feedbackTargets != b.feedbackTargets ||
			    a.invert != b.invert)
			{
				return false;
			}
		}

		return true;
	}
}
