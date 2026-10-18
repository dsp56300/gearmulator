#pragma once

#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "juceRmlUi/rmlDataProvider.h"

namespace pluginManager
{
	// Serves embedded binary data (RML, RCSS, fonts) and also runtime-downloaded images
	class InstallerDataProvider : public juceRmlUi::DataProvider
	{
	public:
		const char* getResourceByFilename(const std::string& _name, uint32_t& _dataSize) override;
		std::vector<std::string> getAllFilenames() override;

		// Adds a downloaded image, and keeps the first one that arrives under a name.
		//
		// Not the last: getResourceByFilename hands out a pointer into this and lets go of the lock,
		// so replacing what is under a name frees memory that whoever asked may still be reading.
		// Nothing here ever needs to replace one - a picture is downloaded once and is the same file
		// every time - so the cheap guarantee is that an entry, once made, stays put.
		void addImage(const std::string& _name, std::vector<char> _data);
		bool hasImage(const std::string& _name) const;

	private:
		const char* findEmbeddedResource(const std::string& _name, uint32_t& _dataSize) const;

		mutable std::mutex m_imageMutex;
		std::map<std::string, std::vector<char>> m_imageCache;
	};
}
