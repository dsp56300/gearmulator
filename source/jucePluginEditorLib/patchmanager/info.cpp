#include "info.h"

#include "../../jucePluginLib/patchdb/patch.h"

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
		m_source->setText(toText(_patch->source), juce::sendNotification);
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

	std::string Info::toText(const pluginLib::patchDB::DataSourcePtr& _source)
	{
		if (!_source)
			return {};

		switch (_source->type)
		{
		case pluginLib::patchDB::SourceType::Invalid:
			return {};
		case pluginLib::patchDB::SourceType::Rom:
		case pluginLib::patchDB::SourceType::File:
		case pluginLib::patchDB::SourceType::Folder:
			return _source->name;
		}
		return {};
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
			fb.items.add(juce::FlexItem(*cChild).withWidth(static_cast<float>(getWidth())).withHeight(40));

		fb.performLayout(m_content.getLocalBounds());
	}

	void Info::resized()
	{
		m_content.setSize(getWidth(), getHeight());
		doLayout();
	}
}
