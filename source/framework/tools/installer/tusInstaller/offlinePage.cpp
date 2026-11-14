#include "offlinePage.h"

#include <algorithm>

#include "tickChip.h"

#include "pluginManagerLib/config.h"
#include "pluginManagerLib/platformUtils.h"

#include "juceRmlUi/juceRmlComponent.h"
#include "juceRmlUi/rmlEventListener.h"
#include "juceRmlUi/rmlInterfaces.h"

#include "RmlUi/Core/Element.h"
#include "RmlUi/Core/ElementDocument.h"
#include "RmlUi/Core/Elements/ElementFormControlInput.h"

#include <JuceHeader.h>

namespace pluginManager
{
	OfflinePage::OfflinePage(Rml::ElementDocument& _doc, PluginManager& _pluginManager, juceRmlUi::RmlComponent& _rmlComponent)
		: m_doc(_doc)
		, m_pluginManager(_pluginManager)
		, m_rmlComponent(_rmlComponent)
		, m_page(_doc.GetElementById("offline-page"))
		, m_back(_doc.GetElementById("offline-back"))
		, m_source(_doc.GetElementById("offline-source"))
		, m_online(_doc.GetElementById("offline-online"))
		, m_folderInput(dynamic_cast<Rml::ElementFormControlInput*>(_doc.GetElementById("offline-folder")))
		, m_systems(_doc.GetElementById("offline-systems"))
		, m_formatChips(_doc.GetElementById("offline-formats"))
		, m_productChips(_doc.GetElementById("offline-products"))
		, m_manager(_doc.GetElementById("offline-manager"))
		, m_write(_doc.GetElementById("offline-write"))
		, m_status(_doc.GetElementById("offline-status"))
		, m_progress(_doc.GetElementById("offline-progress"))
	{
		const auto onClick = [](Rml::Element* _element, std::function<void()> _func)
		{
			if (_element)
				juceRmlUi::EventListener::Add(_element, Rml::EventId::Click, [func = std::move(_func)](Rml::Event&) { func(); });
		};

		// What these start is a file browser or a new catalog, neither of which belongs inside an RmlUi
		// handler.
		onClick(_doc.GetElementById("offline-open"), [this] { juce::MessageManager::callAsync([this, alive = m_alive] { if (*alive) chooseSource(); }); });
		onClick(_doc.GetElementById("offline-browse"), [this] { juce::MessageManager::callAsync([this, alive = m_alive] { if (*alive) browse(); }); });
		onClick(m_online, [this]
		{
			juce::MessageManager::callAsync([this, alive = m_alive]
			{
				if (*alive && m_onSource)
					m_onSource({});
			});
		});

		onClick(m_manager, [this]
		{
			m_withManager = !m_withManager;
			m_manager->SetClass("checked", m_withManager);
			update();
		});

		onClick(m_write, [this] { onWrite(); });

		if (m_folderInput)
		{
			juceRmlUi::EventListener::Add(m_folderInput, Rml::EventId::Change, [this](Rml::Event&)
			{
				m_folder = m_folderInput->GetValue();
				update();
			});
		}
	}

	OfflinePage::~OfflinePage()
	{
		*m_alive = false;

		if (m_cancel)
			*m_cancel = true;
	}

	void OfflinePage::refresh(const Ticked& _ticked)
	{
		const auto& local = m_pluginManager.getLocalFolder();

		if (m_source)
			m_source->SetInnerRML(Rml::StringUtilities::EncodeRml(local.empty() ? "the build server" : local));

		if (m_online)
			m_online->SetClass("hidden", local.empty());

		const auto all = systems();

		// What the list has ticked, the first time. A format this system has no builds in - AU, from
		// Windows - is not in the list at all, and is taken to be wanted where there is one.
		if (!m_initialised)
		{
			m_initialised = true;
			m_folder = juce::File(juce::String(PlatformUtils::dataFolder("Offline install"))).getFullPathName().toStdString();
			m_os = PlatformUtils::getCurrentOS();
			m_arch = PlatformUtils::getCurrentArch();
			m_products = _ticked.products;
			m_formats = _ticked.formats;

			const auto& assets = m_pluginManager.getCatalog().getAssets();

			std::set<std::string> here;

			for (const auto& asset : assets)
			{
				if (matchesPlatform(asset, m_os, m_arch))
					here.insert(asset.format);
			}

			for (const auto& asset : assets)
			{
				if (!here.count(asset.format))
					m_formats.insert(asset.format);
			}
		}

		// The catalog may have changed under it, a folder to install from is the catalog.
		const auto known = std::any_of(all.begin(), all.end(), [this](const System& _s) { return _s.os == m_os && _s.arch == m_arch; });

		if (!known && !all.empty())
		{
			m_os = all.front().os;
			m_arch = all.front().arch;
		}

		if (m_folderInput)
			m_folderInput->SetValue(m_folder);

		build();
	}

	std::vector<OfflinePage::System> OfflinePage::systems() const
	{
		std::vector<System> result;

		for (const auto& asset : m_pluginManager.getCatalog().getAssets())
		{
			if (std::any_of(result.begin(), result.end(), [&](const System& _s) { return _s.os == asset.os && _s.arch == asset.arch; }))
				continue;

			System s{asset.os, asset.arch, {}};

			if (s.os == "windows")		s.label = "Windows";
			else if (s.os == "macos")	s.label = "macOS";
			else if (s.os == "linux")	s.label = "Linux";
			else						s.label = s.os;

			if (s.arch == "aarch64")
				s.label += " ARM";
			else if (s.arch != "x86_64" && s.arch != "universal")
				s.label += " " + s.arch;

			result.push_back(std::move(s));
		}

		// Windows, macOS, Linux, Intel before ARM.
		const auto rank = [](const System& _s)
		{
			const int os = _s.os == "windows" ? 0 : _s.os == "macos" ? 1 : _s.os == "linux" ? 2 : 3;
			return os * 2 + (_s.arch == "aarch64" ? 1 : 0);
		};

		std::stable_sort(result.begin(), result.end(), [&rank](const System& _a, const System& _b) { return rank(_a) < rank(_b); });

		return result;
	}

	std::vector<std::string> OfflinePage::products() const
	{
		const auto& catalog = m_pluginManager.getCatalog();

		std::vector<std::string> result;

		for (const auto& product : catalog.getProducts())
		{
			// It has a chip of its own below.
			if (isAppProduct(product.product))
				continue;

			if (!catalog.getFormats(product.product, m_os, m_arch).empty())
				result.push_back(product.product);

			const auto fx = catalog.fxProductOf(product.product);

			if (!fx.empty() && !catalog.getFormats(fx, m_os, m_arch).empty())
				result.push_back(fx);
		}

		std::stable_sort(result.begin(), result.end(), [this](const std::string& _a, const std::string& _b)
		{
			return juce::String(nameOf(_a)).compareIgnoreCase(juce::String(nameOf(_b))) < 0;
		});

		result.erase(std::unique(result.begin(), result.end()), result.end());

		return result;
	}

	std::vector<std::string> OfflinePage::formats() const
	{
		const auto& catalog = m_pluginManager.getCatalog();

		std::vector<std::string> result;

		for (const auto& product : products())
		{
			for (const auto& format : catalog.getFormats(product, m_os, m_arch))
			{
				if (std::find(result.begin(), result.end(), format) == result.end())
					result.push_back(format);
			}
		}

		return result;
	}

	std::string OfflinePage::nameOf(const std::string& _product) const
	{
		const auto& catalog = m_pluginManager.getCatalog();

		const auto named = [](const Product& _p) { return _p.name.empty() ? _p.product : _p.name; };

		if (const auto* product = catalog.findProduct(_product); product && product->product == _product)
			return named(*product);

		// An effect build is no product of the list, it is named after its plugin.
		if (const auto* owner = catalog.findOwningProduct(_product))
			return named(*owner) + " FX";

		return _product;
	}

	PluginManager::OfflineExport OfflinePage::request() const
	{
		PluginManager::OfflineExport r;
		r.folder = m_folder;
		r.os = m_os;
		r.arch = m_arch;
		r.withPluginManager = m_withManager;

		for (const auto& product : products())
		{
			if (m_products.count(product))
				r.products.push_back(product);
		}

		for (const auto& format : formats())
		{
			if (m_formats.count(format))
				r.formats.push_back(format);
		}

		return r;
	}

	void OfflinePage::addChip(Rml::Element& _parent, const std::string& _name, const bool _checked, std::function<void()> _onClick)
	{
		auto* chip = _parent.AppendChild(m_doc.CreateElement("span"));

		if (!chip)
			return;

		chip->SetClassNames(_checked ? "format-label checked" : "format-label");
		chip->SetInnerRML(tickChipRml(_name));

		juceRmlUi::EventListener::Add(chip, Rml::EventId::Click, [onClick = std::move(_onClick)](Rml::Event&) { onClick(); });
	}

	void OfflinePage::build()
	{
		const auto clear = [](Rml::Element* _e)
		{
			if (!_e)
				return;

			while (_e->GetNumChildren() > 0)
				_e->RemoveChild(_e->GetChild(0));
		};

		clear(m_systems);
		clear(m_formatChips);
		clear(m_productChips);

		// A system is picked rather than ticked: one folder is for one computer. Picking one builds the
		// chips again, which removes the one that was clicked, so not from inside its own handler.
		if (m_systems)
		{
			for (const auto& system : systems())
			{
				addChip(*m_systems, system.label, system.os == m_os && system.arch == m_arch, [this, system]
				{
					juce::MessageManager::callAsync([this, system, alive = m_alive]
					{
						if (!*alive || m_cancel)
							return;

						m_os = system.os;
						m_arch = system.arch;
						withAccess([this] { build(); });
					});
				});
			}
		}

		// A tick is remembered by name rather than per system, so a format or product ticked for one
		// system stays ticked for the next.
		const auto toggle = [this](std::set<std::string>& _set, const std::string& _name, Rml::Element* _parent)
		{
			const auto index = _parent->GetNumChildren();

			return [this, &_set, _name, _parent, index]
			{
				const auto on = !_set.count(_name);

				if (on)
					_set.insert(_name);
				else
					_set.erase(_name);

				if (auto* chip = _parent->GetChild(index))
					chip->SetClass("checked", on);

				update();
			};
		};

		if (m_formatChips)
		{
			for (const auto& format : formats())
				addChip(*m_formatChips, format, m_formats.count(format) != 0, toggle(m_formats, format, m_formatChips));
		}

		if (m_productChips)
		{
			for (const auto& product : products())
				addChip(*m_productChips, nameOf(product), m_products.count(product) != 0, toggle(m_products, product, m_productChips));
		}

		if (m_manager)
			m_manager->SetClass("checked", m_withManager);

		update();
	}

	// How much a click on Write would fetch, worked out from the catalog alone.
	void OfflinePage::update() const
	{
		if (m_cancel)
			return;

		const auto count = m_pluginManager.offlineBuilds(request()).size();
		const auto possible = count > 0 && !m_folder.empty();

		if (m_write)
		{
			m_write->SetClass("disabled", !possible);
			m_write->SetInnerRML("Write the folder");
		}

		if (m_status)
		{
			const auto text = m_folder.empty() ? std::string("Choose a folder first")
				: count == 0 ? std::string("Nothing ticked that this system has builds of")
				: std::to_string(count) + (count == 1 ? " build" : " builds");

			m_status->SetInnerRML(Rml::StringUtilities::EncodeRml(text));
		}

		m_rmlComponent.enqueueUpdate();
	}

	void OfflinePage::withAccess(const std::function<void()>& _func) const
	{
		{
			juceRmlUi::RmlInterfaces::ScopedAccess sa(m_rmlComponent);
			_func();
		}

		m_rmlComponent.enqueueUpdate();
	}

	void OfflinePage::browse()
	{
		m_chooser = std::make_unique<juce::FileChooser>("Folder to prepare the offline install in", juce::File(juce::String(m_folder)));

		m_chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
			[this, alive = m_alive](const juce::FileChooser& _chooser)
			{
				if (!*alive)
					return;

				const auto result = _chooser.getResult();

				if (result == juce::File())
					return;

				withAccess([&]
				{
					m_folder = result.getFullPathName().toStdString();

					if (m_folderInput)
						m_folderInput->SetValue(m_folder);

					update();
				});
			});
	}

	void OfflinePage::chooseSource()
	{
		const auto start = m_pluginManager.getLocalFolder().empty() ? m_folder : m_pluginManager.getLocalFolder();

		m_chooser = std::make_unique<juce::FileChooser>("Folder to install from", juce::File(juce::String(start)));

		m_chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
			[this, alive = m_alive](const juce::FileChooser& _chooser)
			{
				if (!*alive)
					return;

				const auto result = _chooser.getResult();

				if (result == juce::File() || !m_onSource)
					return;

				m_onSource(result.getFullPathName().toStdString());
			});
	}

	void OfflinePage::onWrite()
	{
		// The button is Cancel while a folder is written.
		if (m_cancel)
		{
			*m_cancel = true;
			return;
		}

		const auto r = request();

		if (r.folder.empty() || m_pluginManager.offlineBuilds(r).empty())
			return;

		m_cancel = std::make_shared<bool>(false);

		if (m_write)
		{
			m_write->SetClass("disabled", false);
			m_write->SetInnerRML("Cancel");
		}

		if (m_progress)
		{
			m_progress->SetAttribute("value", 0);
			m_progress->SetClass("active", true);
		}

		m_pluginManager.exportOfflineAsync(r,
			[this, alive = m_alive](const float _progress, const std::string& _what)
			{
				if (!*alive)
					return;

				withAccess([&]
				{
					if (m_progress)
						m_progress->SetAttribute("value", static_cast<int>(_progress * 100.0f));

					if (m_status && !_what.empty())
						m_status->SetInnerRML(Rml::StringUtilities::EncodeRml(_what));
				});
			},
			m_cancel,
			[this, alive = m_alive](const bool _success, const std::string& _message)
			{
				if (!*alive)
					return;

				m_cancel.reset();

				withAccess([&]
				{
					if (m_progress)
						m_progress->SetClass("active", false);

					update();

					if (m_status)
						m_status->SetInnerRML(Rml::StringUtilities::EncodeRml(_message));
				});

				if (!_success)
				{
					juce::NativeMessageBox::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
						"The offline install was not written", juce::String(_message));
				}
			});
	}
}
