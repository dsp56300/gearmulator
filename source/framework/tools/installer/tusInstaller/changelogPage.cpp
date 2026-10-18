#include "changelogPage.h"

#include "juceRmlUi/juceRmlComponent.h"
#include "juceRmlUi/rmlEventListener.h"
#include "juceRmlUi/rmlInterfaces.h"

#include "RmlUi/Core/Element.h"
#include "RmlUi/Core/ElementDocument.h"
#include "RmlUi/Core/StringUtilities.h"

#include <cctype>

#include <JuceHeader.h>

namespace pluginManager
{
	namespace
	{
		std::string encode(const std::string& _text)
		{
			return Rml::StringUtilities::EncodeRml(_text);
		}

		std::string entriesRml(const changelogLib::Lines& _lines)
		{
			std::string rml;

			for (const auto& entry : changelogLib::Changelog::getEntries(_lines))
			{
				rml += "<div class=\"changelog-entry\">";

				if (!entry.tag.empty())
				{
					// The stylesheet colours the ones it knows; anything else in brackets still reads.
					std::string kind;

					for (const auto c : entry.tag)
					{
						if (std::isalnum(static_cast<unsigned char>(c)))
							kind += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
					}

					rml += "<span class=\"changelog-tag tag-" + kind + "\">" + encode(entry.tag) + "</span>";
				}

				rml += "<span class=\"changelog-entry-text\">" + encode(entry.text) + "</span></div>";
			}

			return rml;
		}
	}

	ChangelogPage::ChangelogPage(Rml::ElementDocument& _doc, juceRmlUi::RmlComponent& _rmlComponent)
		: m_doc(_doc)
		, m_rmlComponent(_rmlComponent)
		, m_page(_doc.GetElementById("changelog-page"))
		, m_back(_doc.GetElementById("changelog-back"))
		, m_name(_doc.GetElementById("changelog-name"))
		, m_status(_doc.GetElementById("changelog-status"))
		, m_versions(_doc.GetElementById("changelog-versions"))
		, m_text(_doc.GetElementById("changelog-text"))
	{
		for (const auto& [id, markdown] : {std::make_pair("changelog-copy-text", false), std::make_pair("changelog-copy-markdown", true)})
		{
			if (auto* button = _doc.GetElementById(id))
			{
				juceRmlUi::EventListener::Add(button, Rml::EventId::Click, [this, markdown = markdown](Rml::Event&)
				{
					copy(markdown);
					m_rmlComponent.enqueueUpdate();
				});
			}
		}
	}

	ChangelogPage::~ChangelogPage()
	{
		*m_alive = false;
	}

	void ChangelogPage::show(const Product& _product, const PluginManager& _pluginManager)
	{
		m_product = _product;

		clear();

		if (m_name)
			m_name->SetInnerRML(encode((_product.name.empty() ? _product.product : _product.name) + " - Changelog"));

		setStatus("Loading...");

		const auto request = ++m_request;

		_pluginManager.fetchChangelogAsync(_product.product,
			[this, alive = m_alive, request](const bool _success, const changelogLib::Changelog& _changelog, const std::string& _error)
		{
			if (!*alive || request != m_request)
				return;

			juceRmlUi::RmlInterfaces::ScopedAccess sa(m_rmlComponent);

			if (_success)
				onChangelog(_changelog);
			else
				setStatus(_error);

			m_rmlComponent.enqueueUpdate();
		});
	}

	void ChangelogPage::onChangelog(const changelogLib::Changelog& changelog)
	{
		// The build's name first, which is what the changelog is written with; the name on the row
		// is what somebody may have called it instead.
		m_section = changelog.findProduct(m_product.product);

		if (m_section.empty() && !m_product.name.empty())
			m_section = changelog.findProduct(m_product.name);

		m_releases.clear();

		for (auto& release : changelog.getReleases(m_section))
		{
			// Text before the first version heading, which a release body starts with.
			if (!release.version.empty())
				m_releases.push_back(std::move(release));
		}

		if (m_releases.empty())
		{
			setStatus("The changelog has nothing about this product yet.");
			return;
		}

		setStatus({});

		if (!m_versions)
			return;

		for (size_t i = 0; i < m_releases.size(); ++i)
		{
			auto* element = m_versions->AppendChild(m_doc.CreateElement("div"));

			if (!element)
				continue;

			element->SetClassNames("changelog-version");
			element->SetInnerRML(encode(m_releases[i].version));

			juceRmlUi::EventListener::Add(element, Rml::EventId::Click, [this, i](Rml::Event&)
			{
				select(i);
				m_rmlComponent.enqueueUpdate();
			});

			m_versionElements.push_back(element);
		}

		select(0);
	}

	void ChangelogPage::select(const size_t _index)
	{
		if (_index >= m_releases.size() || !m_text)
			return;

		m_selected = _index;

		for (size_t i = 0; i < m_versionElements.size(); ++i)
			m_versionElements[i]->SetClass("selected", i == _index);

		const auto& release = m_releases[_index];
		const auto name = m_product.name.empty() ? m_product.product : m_product.name;

		std::string rml = "<div class=\"changelog-version-title\">Version " + encode(release.version) + "</div>";

		rml += "<div class=\"changelog-section-title\">" + encode(name) + "</div>";

		if (release.product.empty())
			rml += "<div class=\"changelog-none\">Nothing in this version is about " + encode(name) + " alone.</div>";
		else
			rml += entriesRml(release.product);

		if (!release.global.empty())
		{
			rml += "<div class=\"changelog-section-title changelog-global\">Framework - every plugin</div>";

			// The framework itself first, then the parts of it that have a heading of their own.
			if (const auto it = release.global.find("Framework"); it != release.global.end())
				rml += entriesRml(it->second);

			for (const auto& [section, lines] : release.global)
			{
				if (section == "Framework")
					continue;

				rml += "<div class=\"changelog-subsection\">" + encode(section) + "</div>";
				rml += entriesRml(lines);
			}
		}

		m_text->SetInnerRML(rml);
		m_text->SetScrollTop(0);
	}

	void ChangelogPage::copy(const bool _markdown) const
	{
		if (m_selected >= m_releases.size())
			return;

		const auto& release = m_releases[m_selected];

		juce::SystemClipboard::copyTextToClipboard(juce::String(_markdown
			? changelogLib::Changelog::toMarkdown(release, m_section)
			: changelogLib::Changelog::toText(release, m_section)));

		setStatus("Version " + release.version + (_markdown ? " copied as Markdown" : " copied as text"));
	}

	void ChangelogPage::setStatus(const std::string& _text) const
	{
		if (m_status)
			m_status->SetInnerRML(encode(_text));
	}

	void ChangelogPage::clear()
	{
		m_section.clear();
		m_releases.clear();
		m_versionElements.clear();
		m_selected = 0;

		if (m_versions)
		{
			while (m_versions->GetNumChildren() > 0)
				m_versions->RemoveChild(m_versions->GetFirstChild());
		}

		if (m_text)
			m_text->SetInnerRML({});
	}
}
