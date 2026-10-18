#include "installerDataProvider.h"

#include "BinaryData.h"

namespace pluginManager
{
	const char* InstallerDataProvider::getResourceByFilename(const std::string& _name, uint32_t& _dataSize)
	{
		// First check embedded binary data
		const auto* result = findEmbeddedResource(_name, _dataSize);
		if (result)
			return result;

		// Then the downloaded pictures. What comes back points into the cache and outlives the lock,
		// which is safe only because an entry is never replaced once it is in - see addImage.
		std::lock_guard lock(m_imageMutex);
		const auto it = m_imageCache.find(_name);
		if (it != m_imageCache.end())
		{
			_dataSize = static_cast<uint32_t>(it->second.size());
			return it->second.data();
		}

		return nullptr;
	}

	std::vector<std::string> InstallerDataProvider::getAllFilenames()
	{
		std::vector<std::string> names;

		for (int i = 0; i < BinaryData::namedResourceListSize; ++i)
			names.emplace_back(BinaryData::originalFilenames[i]);

		std::lock_guard lock(m_imageMutex);
		for (const auto& [name, _] : m_imageCache)
			names.push_back(name);

		return names;
	}

	void InstallerDataProvider::addImage(const std::string& _name, std::vector<char> _data)
	{
		std::lock_guard lock(m_imageMutex);

		// First one wins, so that what was handed out stays valid - see the header.
		m_imageCache.emplace(_name, std::move(_data));
	}

	bool InstallerDataProvider::hasImage(const std::string& _name) const
	{
		std::lock_guard lock(m_imageMutex);
		return m_imageCache.find(_name) != m_imageCache.end();
	}

	const char* InstallerDataProvider::findEmbeddedResource(const std::string& _name, uint32_t& _dataSize) const
	{
		for (int i = 0; i < BinaryData::namedResourceListSize; ++i)
		{
			if (_name == BinaryData::originalFilenames[i])
			{
				int size = 0;
				const auto* data = BinaryData::getNamedResource(BinaryData::namedResourceList[i], size);
				_dataSize = static_cast<uint32_t>(size);
				return data;
			}
		}
		return nullptr;
	}
}
