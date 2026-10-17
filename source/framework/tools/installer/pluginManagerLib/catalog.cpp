#include "catalog.h"

#include <algorithm>
#include <cctype>

namespace pluginManager
{
	namespace
	{
		// Shown in this order rather than however the assets happened to arrive.
		const char* const g_formatOrder[] = {"VST3", "VST2", "CLAP", "LV2", "AU", "Standalone"};

		size_t formatRank(const std::string& _format)
		{
			for (size_t i = 0; i < std::size(g_formatOrder); ++i)
			{
				if (_format == g_formatOrder[i])
					return i;
			}

			return std::size(g_formatOrder);
		}

		// So that case does not decide the order.
		std::string toLower(std::string _s)
		{
			for (auto& c : _s)
				c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

			return _s;
		}
	}

	bool sortsBefore(const Product& _a, const Product& _b)
	{
		// What is missing goes last.
		if (_a.manufacturer.empty() != _b.manufacturer.empty())
			return !_a.manufacturer.empty();

		const auto a = toLower(_a.manufacturer);
		const auto b = toLower(_b.manufacturer);

		if (a != b)
			return a < b;

		if ((_a.year == 0) != (_b.year == 0))
			return _a.year != 0;

		return _a.year < _b.year;
	}

	bool matchesPlatform(const Asset& _asset, const std::string& _os, const std::string& _arch)
	{
		if (_asset.os != _os)
			return false;

		// A universal build covers every arch on that OS, and a machine reporting "universal"
		// accepts anything built for it.
		return _asset.arch == _arch || _asset.arch == "universal" || _arch == "universal";
	}

	void Catalog::addAssets(const std::vector<Asset>& _assets)
	{
		m_assets.insert(m_assets.end(), _assets.begin(), _assets.end());
		resolveDataProducts();
	}

	void Catalog::clear()
	{
		m_assets.clear();
	}

	std::string Catalog::getNewestPublishedVersion() const
	{
		std::string newest;

		for (const auto& asset : m_assets)
		{
			if (asset.channel == Channel::Public && (newest.empty() || compareVersions(asset.version, newest) > 0))
				newest = asset.version;
		}

		return newest;
	}

	bool Catalog::isPublished(const std::string& _product) const
	{
		const auto* owner = findOwningProduct(_product);
		const auto& base = owner ? owner->product : _product;

		return std::any_of(m_assets.begin(), m_assets.end(), [&](const Asset& _asset)
		{
			if (_asset.channel != Channel::Public)
				return false;

			const auto* assetOwner = findOwningProduct(_asset.product);
			return (assetOwner ? assetOwner->product : _asset.product) == base;
		});
	}

	void Catalog::setProducts(std::vector<Product> _products)
	{
		m_products = std::move(_products);
		resolveDataProducts();
	}

	const Product* Catalog::findProduct(const std::string& _product) const
	{
		for (const auto& p : m_products)
		{
			if (p.product == _product)
				return &p;
		}

		return nullptr;
	}

	const Product* Catalog::findOwningProduct(const std::string& _product) const
	{
		const Product* best = nullptr;

		// The longest match wins, so a product that is itself the prefix of another one does not claim
		// the other one's builds.
		for (const auto& p : m_products)
		{
			if (p.product.size() > _product.size() || _product.compare(0, p.product.size(), p.product) != 0)
				continue;

			if (!best || p.product.size() > best->product.size())
				best = &p;
		}

		return best;
	}

	void Catalog::addProducts(const std::vector<Product>& _products)
	{
		for (const auto& p : _products)
		{
			const auto known = std::find_if(m_products.begin(), m_products.end(), [&](const Product& _known)
			{
				return _known.product == p.product;
			});

			if (known == m_products.end())
			{
				m_products.push_back(p);
				continue;
			}

			known->files.insert(known->files.end(), p.files.begin(), p.files.end());

			// The public list is not ours to edit, so the internal one may say what it does not.
			if (known->category.empty())
				known->category = p.category;

			if (known->manufacturer.empty())
				known->manufacturer = p.manufacturer;

			if (known->originalName.empty())
				known->originalName = p.originalName;

			if (!known->year)
				known->year = p.year;
		}

		resolveDataProducts();
	}

	void Catalog::setChannelFiles(const std::string& _slug, const Channel _channel, std::vector<FileSection> _files)
	{
		m_channelFiles[{_slug, _channel}] = std::move(_files);
	}

	const std::vector<FileSection>* Catalog::getChannelFiles(const std::string& _slug, const Channel _channel) const
	{
		const auto it = m_channelFiles.find({_slug, _channel});

		return it == m_channelFiles.end() ? nullptr : &it->second;
	}

	std::string Catalog::dataProductOf(const std::string& _product) const
	{
		// The plugin drops FX from an effect's name and keeps a synth's whole. The product lists name
		// the synths, so a build is an effect build of one when it is that name with FX after it - and
		// a product whose own name ends in FX is its own product, with a folder of its own.
		const auto* owner = findOwningProduct(_product);

		return owner && _product == owner->product + "FX" ? owner->product : _product;
	}

	void Catalog::resolveDataProducts()
	{
		for (auto& a : m_assets)
			a.dataProduct = dataProductOf(a.product);
	}

	std::vector<std::string> Catalog::getFormats(const std::string& _product, const std::string& _os, const std::string& _arch) const
	{
		std::vector<std::string> formats;

		for (const auto& a : m_assets)
		{
			if (a.product != _product || a.format.empty() || !matchesPlatform(a, _os, _arch))
				continue;

			if (std::find(formats.begin(), formats.end(), a.format) == formats.end())
				formats.push_back(a.format);
		}

		std::sort(formats.begin(), formats.end(), [](const std::string& _a, const std::string& _b)
		{
			return formatRank(_a) < formatRank(_b);
		});

		return formats;
	}

	std::vector<std::string> Catalog::getVersions(const std::string& _product, const std::string& _format,
	                                              const std::string& _os, const std::string& _arch) const
	{
		std::vector<std::string> versions;

		for (const auto& a : m_assets)
		{
			if (a.product != _product || a.format != _format || !matchesPlatform(a, _os, _arch))
				continue;

			if (std::find(versions.begin(), versions.end(), a.version) == versions.end())
				versions.push_back(a.version);
		}

		std::sort(versions.begin(), versions.end(), [](const std::string& _a, const std::string& _b)
		{
			return compareVersions(_a, _b) > 0;
		});

		return versions;
	}

	std::vector<std::string> Catalog::getVariants(const std::string& _product) const
	{
		std::vector<std::string> variants;

		for (const auto& a : m_assets)
		{
			if (a.product.size() <= _product.size() || a.product.compare(0, _product.size(), _product) != 0)
				continue;

			if (std::find(variants.begin(), variants.end(), a.product) == variants.end())
				variants.push_back(a.product);
		}

		std::sort(variants.begin(), variants.end());

		return variants;
	}

	std::string Catalog::fxProductOf(const std::string& _product) const
	{
		const auto fx = _product + "FX";

		for (const auto& variant : getVariants(_product))
		{
			if (variant == fx)
				return variant;
		}

		return {};
	}

	Channel Catalog::getChannel(const std::string& _product, const std::string& _format, const std::string& _version,
	                            const std::string& _os, const std::string& _arch) const
	{
		// Channels are declared from the widest audience to the narrowest, so the smallest one wins.
		auto channel = Channel::Internal;

		for (const auto& a : m_assets)
		{
			if (a.product == _product && a.format == _format && a.version == _version && matchesPlatform(a, _os, _arch))
				channel = std::min(channel, a.channel);
		}

		return channel;
	}

	const Asset* Catalog::find(const std::string& _product, const std::string& _format, const std::string& _version,
	                           const std::string& _os, const std::string& _arch) const
	{
		// Lower is better: the channel first, because a public build needs no login, then the package.
		const auto rank = [](const Asset& _a)
		{
			return static_cast<int>(_a.channel) * 2 + (_a.package == "zip" ? 0 : 1);
		};

		const Asset* best = nullptr;

		for (const auto& a : m_assets)
		{
			if (a.product != _product || a.format != _format || a.version != _version || !matchesPlatform(a, _os, _arch))
				continue;

			if (!best || rank(a) < rank(*best))
				best = &a;
		}

		return best;
	}
}
