#include "info.h"

#include "patchmanager.h"
#include "defaultskin.h"

#include "../../jucePluginLib/patchdb/patch.h"
#include "../../juceUiLib/uiObject.h"
#include "../pluginEditor.h"

namespace jucePluginEditorLib::patchManager
{
	Info::Info(PatchManager& _pm) : m_patchManager(_pm)
	{
		addAndMakeVisible(m_content);

		m_name = addChild(new juce::Label());
		m_lbSource = addChild(new juce::Label("", "Source"));
		m_source = addChild(new juce::Label());
		m_lbCategories = addChild(new juce::Label("", "Categories"));
		m_categories = addChild(new juce::Label());
		m_lbTags = addChild(new juce::Label("", "Tags"));
		m_tags = addChild(new juce::Label());

		if(const auto& t = _pm.getTemplate("pm_info_label"))
		{
			t->apply(_pm.getEditor(), *m_lbSource);
			t->apply(_pm.getEditor(), *m_lbCategories);
			t->apply(_pm.getEditor(), *m_lbTags);
		}
		else
		{
			m_lbSource->setColour(juce::Label::textColourId, juce::Colour(defaultSkin::colors::infoLabel));
			m_lbCategories->setColour(juce::Label::textColourId, juce::Colour(defaultSkin::colors::infoLabel));
			m_lbTags->setColour(juce::Label::textColourId, juce::Colour(defaultSkin::colors::infoLabel));

			m_lbSource->setJustificationType(juce::Justification::bottomLeft);
			m_lbCategories->setJustificationType(juce::Justification::bottomLeft);
			m_lbTags->setJustificationType(juce::Justification::bottomLeft);
		}

		if (const auto& t = _pm.getTemplate("pm_info_text"))
		{
			t->apply(_pm.getEditor(), *m_source);
			t->apply(_pm.getEditor(), *m_categories);
			t->apply(_pm.getEditor(), *m_tags);
		}
		else
		{
			m_source->setColour(juce::Label::textColourId, juce::Colour(defaultSkin::colors::infoText));
			m_categories->setColour(juce::Label::textColourId, juce::Colour(defaultSkin::colors::infoText));
			m_tags->setColour(juce::Label::textColourId, juce::Colour(defaultSkin::colors::infoText));

			m_source->setJustificationType(juce::Justification::topLeft);
			m_categories->setJustificationType(juce::Justification::topLeft);
			m_tags->setJustificationType(juce::Justification::topLeft);
		}

		if (const auto& t = _pm.getTemplate("pm_info_name"))
		{
			t->apply(_pm.getEditor(), *m_name);
		}
		else
		{
			auto f = m_name->getFont();
			f.setHeight(f.getHeight() * 2);
			f.setBold(true);
			m_name->setFont(f);
			m_name->setJustificationType(juce::Justification::topLeft);
			m_name->setColour(juce::Label::textColourId, juce::Colour(defaultSkin::colors::infoHeadline));
			m_name->setColour(juce::Label::backgroundColourId, juce::Colour(defaultSkin::colors::background));
		}
	}

	Info::~Info()
	{
		m_content.deleteAllChildren();
	}

	void Info::setPatch(const pluginLib::patchDB::PatchPtr& _patch) const
	{
		if (!_patch)
		{
			clear();
			return;
		}

		m_name->setText(_patch->getName(), juce::sendNotification);
		m_source->setText(toText(_patch->source.lock()), juce::sendNotification);
		m_categories->setText(toText(_patch->getTags().get(pluginLib::patchDB::TagType::Category)), juce::sendNotification);
		m_tags->setText(toText(_patch->getTags().get(pluginLib::patchDB::TagType::Tag)), juce::sendNotification);

		doLayout();
	}

	void Info::clear() const
	{
		m_name->setText({}, juce::sendNotification);
		m_source->setText({}, juce::sendNotification);
		m_categories->setText({}, juce::sendNotification);
		m_tags->setText({}, juce::sendNotification);

		doLayout();
	}

	std::string Info::toText(const pluginLib::patchDB::Tags& _tags)
	{
		const auto& tags = _tags.getAdded();
		std::stringstream ss;

		size_t i = 0;
		for (const auto& tag : tags)
		{
			if (i)
				ss << ", ";
			ss << tag;
			++i;
		}
		return ss.str();
	}

	std::string Info::toText(const pluginLib::patchDB::DataSourceNodePtr& _source)
	{
		if (!_source)
			return {};

		switch (_source->type)
		{
		case pluginLib::patchDB::SourceType::Invalid:
		case pluginLib::patchDB::SourceType::Count:
			return {};
		case pluginLib::patchDB::SourceType::Rom:
		case pluginLib::patchDB::SourceType::Folder:
			return _source->name;
		case pluginLib::patchDB::SourceType::File:
			{
				auto t = _source->name;
				const auto pos = t.find_last_of("\\/");
				if (pos != std::string::npos)
					return t.substr(pos + 1);
				return t;
			}
		}
		return {};
	}

	void Info::paint(juce::Graphics& g)
	{
		g.fillAll(m_name->findColour(juce::Label::backgroundColourId));
	}

	juce::Label* Info::addChild(juce::Label* _label)
	{
		m_content.addAndMakeVisible(_label);
		return _label;
	}

	void Info::doLayout() const
	{
		juce::FlexBox fb;
		fb.flexWrap = juce::FlexBox::Wrap::noWrap;
		fb.justifyContent = juce::FlexBox::JustifyContent::flexStart;
		fb.alignContent = juce::FlexBox::AlignContent::flexStart;
		fb.flexDirection = juce::FlexBox::Direction::column;

		for (const auto& cChild : m_content.getChildren())
		{
			juce::FlexItem item(*cChild);
			item = item.withWidth(static_cast<float>(getWidth()));

			const auto* label = dynamic_cast<const juce::Label*>(cChild);
			if (label)
			{
				const auto t = label->getText();
				int lineCount = 1;
				for (const auto ch : t)
				{
					if (ch == '\n')
						++lineCount;
				}
				item = item.withHeight(label->getFont().getHeight() * (static_cast<float>(lineCount) + 1.5f));
			}
			fb.items.add(item);
		}

		fb.performLayout(m_content.getLocalBounds());
	}

	void Info::resized()
	{
		m_content.setSize(getWidth(), getHeight());
		doLayout();
	}
}
