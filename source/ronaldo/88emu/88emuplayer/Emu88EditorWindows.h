#pragma once

#include "Emu88Editor.h"
#include "Emu88EditorBindings.h"
#include "juceRmlUi/juceRmlComponent.h"
#include "juceRmlUi/juceRmlComponentConfig.h"
#include "jucePluginLib/version.h"
#include "jucePluginLib/versionDateTime.h"
#include "RmlUi/Core/ElementDocument.h"
#include "juce_gui_extra/juce_gui_extra.h"

namespace emu88Player
{
	using namespace editor;
	class TitleBarButton final : public juce::TextButton
	{
	public:
		using juce::TextButton::TextButton;

		void setRecording(const bool _recording)
		{
			m_recording = _recording;
			setButtonText(_recording ? g_recordStopLabel : g_recordLabel);
			applyBounds();
		}

		void parentSizeChanged() override
		{
			applyBounds();
		}

		void applyBounds()
		{
			const auto* window = dynamic_cast<juce::DocumentWindow*>(getParentComponent());
			if(!window)
				return;
			const auto height = window->getTitleBarHeight() - 8;
			if(height <= 0)
				return;
			// Sized to its own content, so the glyph and the label stay together
			// instead of drifting apart inside a box wider than either needs.
			setBounds(g_optionsButtonRight + g_titleBarButtonGap, g_titleBarButtonY,
			          iconGutter(height) + textWidth(height) + g_recordIconMargin, height);
		}

		void paintButton(juce::Graphics& _g, const bool _highlighted, const bool _down) override
		{
			auto& lookAndFeel = getLookAndFeel();
			lookAndFeel.drawButtonBackground(_g, *this, findColour(juce::TextButton::buttonColourId),
			                                 _highlighted, _down);

			const auto alpha = isEnabled() ? 1.0f : 0.5f;
			const auto icon = iconBounds(getHeight());
			_g.setColour(juce::Colour(g_recordIconColour).withMultipliedAlpha(alpha));
			if(m_recording)
				_g.fillRect(icon.reduced(icon.getWidth() * 0.1f));	// stop
			else
				_g.fillEllipse(icon);								// record

			// The label is drawn here rather than by the look and feel because it
			// has to centre in what is left of the button, not in the whole box.
			_g.setFont(lookAndFeel.getTextButtonFont(*this, getHeight()));
			_g.setColour(findColour(juce::TextButton::textColourOffId).withMultipliedAlpha(alpha));
			_g.drawFittedText(getButtonText(),
			                  getLocalBounds().withTrimmedLeft(iconGutter(getHeight()))
			                                  .withTrimmedRight(g_recordIconMargin).reduced(0, 2),
			                  juce::Justification::centredLeft, 1);
		}

	private:
		static juce::Rectangle<float> iconBounds(const int _height)
		{
			const auto size = std::max(5.0f, static_cast<float>(_height) * 0.45f);
			return {static_cast<float>(g_recordIconMargin), (static_cast<float>(_height) - size) * 0.5f,
			        size, size};
		}

		static int iconGutter(const int _height)
		{
			return g_recordIconMargin + juce::roundToInt(iconBounds(_height).getWidth()) + g_recordIconGap;
		}

		// Both labels measure the same box, so toggling never moves the button or
		// the text it holds.
		int textWidth(const int _height)
		{
			const auto font = getLookAndFeel().getTextButtonFont(*this, _height);
			return std::max(font.getStringWidth(g_recordLabel), font.getStringWidth(g_recordStopLabel));
		}

		bool m_recording = false;
	};

	class KeyboardWindow final : public juce::DocumentWindow
	{
	public:
		using Row = KeyboardShortcut;
		using Groups = KeyboardShortcutGroups;

		class Content final : public juce::Component
		{
		public:
			explicit Content(Groups _groups) : m_groups(std::move(_groups))
			{
				// Two columns once the list is long enough to be worth splitting,
				// always breaking between groups so a panel area stays together.
				// Short boards (the SC-55mkII has ten rows) stay in one column.
				const auto total = linesIn(0, m_groups.size());
				m_split = total > kSplitThreshold ? balancedSplit() : m_groups.size();
				const auto columns = m_split < m_groups.size() ? 2 : 1;
				const auto rows = columns == 2
					? std::max(linesIn(0, m_split), linesIn(m_split, m_groups.size()))
					: total;

				// The window follows the text rather than the other way round:
				// the Pro's mode-dependent legends are much wider than a name.
				const juce::Font labelFont(11.0f);
				int label = 0;
				for(const auto& group : m_groups)
					for(const auto& row : group.rows)
						label = std::max(label, labelFont.getStringWidth(row.label));
				m_columnWidth = kKeyWidth + kLabelGap + label;
				// A board with only short legends would otherwise produce a window
				// narrower than its own title bar.
				const auto width = std::max(kMinWidth,
					2 * kMargin + columns * m_columnWidth + (columns - 1) * kColumnGap);
				m_columnWidth = (width - 2 * kMargin - (columns - 1) * kColumnGap) / columns;
				setSize(width, 2 * kMargin + rows * kRowHeight);
			}

			void paint(juce::Graphics& _g) override
			{
				_g.fillAll(juce::Colour(0xff17181c));
				const auto area = getLocalBounds().reduced(kMargin);
				paintColumn(_g, area.withWidth(m_columnWidth), 0, m_split);
				if(m_split < m_groups.size())
					paintColumn(_g, area.withTrimmedLeft(m_columnWidth + kColumnGap).withWidth(m_columnWidth),
					            m_split, m_groups.size());
			}

		private:
			static constexpr int kRowHeight = 18;
			static constexpr int kKeyWidth = 78;
			static constexpr int kLabelGap = 8;
			static constexpr int kColumnGap = 20;
			static constexpr int kMargin = 16;
			static constexpr size_t kSplitThreshold = 14;
			static constexpr int kMinWidth = 340;

			// Lines a run of groups occupies: a heading and its rows, plus a half
			// row of air between groups.
			int linesIn(const size_t _first, const size_t _last) const
			{
				int n = 0;
				for(size_t g = _first; g < _last; ++g)
					n += static_cast<int>(m_groups[g].rows.size()) + 1 + (g + 1 < _last ? 1 : 0);
				return n;
			}

			size_t balancedSplit() const
			{
				size_t best = m_groups.size();
				int bestHeight = std::numeric_limits<int>::max();
				for(size_t split = 1; split < m_groups.size(); ++split)
				{
					const auto height = std::max(linesIn(0, split), linesIn(split, m_groups.size()));
					if(height >= bestHeight)
						continue;
					bestHeight = height;
					best = split;
				}
				return best;
			}

			void paintColumn(juce::Graphics& _g, juce::Rectangle<int> _area,
			                 const size_t _first, const size_t _last) const
			{
				for(size_t g = _first; g < _last; ++g)
				{
					if(g > _first)
						_area.removeFromTop(kRowHeight);
					_g.setColour(juce::Colour(0xffff6f0f));
					_g.setFont(juce::Font(11.0f, juce::Font::bold));
					_g.drawText(m_groups[g].heading, _area.removeFromTop(kRowHeight),
					            juce::Justification::centredLeft);

					for(const auto& row : m_groups[g].rows)
					{
						auto line = _area.removeFromTop(kRowHeight);
						const auto key = line.removeFromLeft(kKeyWidth);
						_g.setColour(juce::Colour(0xff23252b));
						_g.fillRoundedRectangle(key.reduced(1, 2).toFloat(), 3.0f);
						_g.setColour(juce::Colours::white);
						_g.setFont(juce::Font(11.0f, juce::Font::bold));
						_g.drawText(row.keys, key, juce::Justification::centred);
						_g.setColour(juce::Colour(0xff9aa0a8));
						_g.setFont(juce::Font(11.0f));
						_g.drawText(row.label, line.withTrimmedLeft(kLabelGap),
						            juce::Justification::centredLeft);
					}
				}
			}

			const Groups m_groups;
			size_t m_split = 0;
			int m_columnWidth = 0;
		};

		KeyboardWindow(Editor& _owner, const std::string& _product, Groups _groups)
			: juce::DocumentWindow(juce::String(_product) + " keyboard shortcuts",
			                       juce::Colour(0xff17181c), juce::DocumentWindow::closeButton),
			  m_owner(_owner), m_content(std::move(_groups))
		{
			setUsingNativeTitleBar(true);
			setResizable(false, false);
			setContentNonOwned(&m_content, true);
			centreAroundComponent(&_owner, getWidth(), getHeight());
		}

		~KeyboardWindow() override
		{
			clearContentComponent();
		}

		void closeButtonPressed() override;

	private:
		Editor& m_owner;
		Content m_content;
	};

	class AboutWindow final : public juce::DocumentWindow
	{
	public:
		class Content final : public juce::Component
		{
		public:
			Content(std::string _product)
				: m_product(std::move(_product))
				, m_website("dsp56300.wordpress.com", juce::URL(g_websiteUrl))
				, m_donate(g_donateUrl, juce::URL(g_donateUrl))
			{
				for(auto* link : {&m_website, &m_donate})
				{
					link->setJustificationType(juce::Justification::centred);
					link->setColour(juce::HyperlinkButton::textColourId, juce::Colour(0xffff6f0f));
					addAndMakeVisible(link);
				}
				setSize(g_aboutWidth, g_aboutHeight);
			}

			// One layout walk shared by paint() and resized(), so the text blocks
			// and the two links cannot drift apart.
			struct Layout
			{
				juce::Rectangle<int> title, version, licence, thanks;
				juce::Rectangle<int> vendor, website, donateLabel, donate;
			};

			static Layout layoutOf(juce::Rectangle<int> _area)
			{
				Layout l;
				l.title   = _area.removeFromTop(30);
				l.version = _area.removeFromTop(20);
				_area.removeFromTop(12);
				l.licence = _area.removeFromTop(32);	// two lines
				_area.removeFromTop(10);
				l.thanks  = _area.removeFromTop(16);	// one line
				_area.removeFromTop(14);
				l.vendor  = _area.removeFromTop(18);
				l.website = _area.removeFromTop(16);
				_area.removeFromTop(10);
				l.donateLabel = _area.removeFromTop(16);
				l.donate  = _area.removeFromTop(16);
				return l;
			}

			void paint(juce::Graphics& _g) override
			{
				_g.fillAll(juce::Colour(0xff17181c));
				const auto l = layoutOf(getLocalBounds().reduced(16));

				_g.setColour(juce::Colours::white);
				_g.setFont(juce::Font(22.0f, juce::Font::bold));
				_g.drawText(m_product, l.title, juce::Justification::centred);

				_g.setColour(juce::Colour(0xffb8bcc4));
				_g.setFont(juce::Font(13.0f));
				_g.drawText("Version " + std::string(g_pluginVersionString) + "  (" +
				            g_pluginVersionDate + ' ' + g_pluginVersionTime + ')',
				            l.version, juce::Justification::centred);

				_g.setColour(juce::Colour(0xff9aa0a8));
				_g.setFont(juce::Font(12.0f));
				_g.drawFittedText("Free software under the GNU General Public License v3,\n"
				                  "with no warranty. See LICENSE.md for the full terms.",
				                  l.licence, juce::Justification::centredTop, 2);
				_g.drawFittedText("Thanks to mamedev, nukeykt, mckuhei, superctr.",
				                  l.thanks, juce::Justification::centred, 1);

				_g.setColour(juce::Colour(0xffb8bcc4));
				_g.setFont(juce::Font(12.0f));
				_g.drawText("The Usual Suspects", l.vendor, juce::Justification::centred);
				_g.drawText("Support development:", l.donateLabel, juce::Justification::centred);
			}

			void resized() override
			{
				const auto l = layoutOf(getLocalBounds().reduced(16));
				m_website.setBounds(l.website);
				m_donate.setBounds(l.donate);
			}

		private:
			const std::string m_product;
			juce::HyperlinkButton m_website;
			juce::HyperlinkButton m_donate;
		};

		AboutWindow(Editor& _owner, std::string _product)
			: juce::DocumentWindow("About " + juce::String(_product), juce::Colour(0xff17181c),
			                       juce::DocumentWindow::closeButton),
			  m_owner(_owner), m_content(std::move(_product))
		{
			setUsingNativeTitleBar(true);
			setResizable(false, false);
			setContentNonOwned(&m_content, true);
			centreAroundComponent(&_owner, getWidth(), getHeight());
		}

		~AboutWindow() override
		{
			clearContentComponent();
		}

		void closeButtonPressed() override;

	private:
		Editor& m_owner;
		Content m_content;
	};

	class SettingsWindow final : public juce::DocumentWindow
	{
	public:
		explicit SettingsWindow(Editor& _owner)
			: juce::DocumentWindow("88emuPlayer Settings", juce::Colours::black,
			                       juce::DocumentWindow::closeButton),
			  m_owner(_owner), m_interfaces(_owner)
		{
			setUsingNativeTitleBar(true);
			setResizable(false, false);
			reloadContent();
			centreAroundComponent(&m_owner, getWidth(), getHeight());
		}

		void reloadContent()
		{
			clearContentComponent();
			m_rml.reset();
			juceRmlUi::RmlComponentConfig config;
			config.refreshRateLimitHz = 30;
			config.includeDefaultTemplates = false;
			config.additionalTemplateFiles.push_back("emu88PlayerSettings.rml");
			const auto software = m_owner.m_processor.config().getIntValue("forceSoftwareRenderer", -1);
			if(software >= 0)
				config.forceSoftwareRenderer = software > 0 ? juceRmlUi::SoftwareRendererMode::ForceOn :
				                                             juceRmlUi::SoftwareRendererMode::ForceOff;
			m_rml = std::make_unique<juceRmlUi::RmlComponent>(
				m_interfaces, m_owner, "emu88PlayerSettingsWindow.rml", 1.0f,
				[](juceRmlUi::RmlComponent&, Rml::Context&) {},
				[](juceRmlUi::RmlComponent&, Rml::Context&) {}, config);
			setContentNonOwned(m_rml.get(), false);
			setContentComponentSize(g_settingsWidth, g_settingsHeight);
		}

		~SettingsWindow() override
		{
			clearContentComponent();
		}

		juceRmlUi::RmlComponent& rmlComponent() const
		{
			return *m_rml;
		}

		Rml::Element* settingsRoot() const
		{
			return m_rml->getDocument()->GetElementById("hardwareSettingsRoot");
		}

		void closeButtonPressed() override
		{
			m_owner.showSettings(false);
		}

	private:
		Editor& m_owner;
		juceRmlUi::RmlInterfaces m_interfaces;
		std::unique_ptr<juceRmlUi::RmlComponent> m_rml;
	};
}
