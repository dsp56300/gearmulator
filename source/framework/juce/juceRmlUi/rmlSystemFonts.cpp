#include "rmlSystemFonts.h"

#include <initializer_list>
#include <map>
#include <mutex>

#include "RmlUi/Core/Core.h"
#include "RmlUi/Core/Log.h"

#include "juce_core/juce_core.h"

namespace juceRmlUi::systemFonts
{
	namespace
	{
		// Use the first installed font in each script group as a fallback.
		using Group = std::vector<juce::File>;

		std::vector<Group> candidateGroups()
		{
#if JUCE_MAC
			const juce::File fonts("/System/Library/Fonts");
			return {
				// Hiragino Sans (ヒラギノ角ゴシック W3): Japanese, so Japanese names get Japanese glyphs
				{fonts.getChildFile(juce::String::fromUTF8("\xe3\x83\x92\xe3\x83\xa9\xe3\x82\xae\xe3\x83\x8e\xe8\xa7\x92"
				                                           "\xe3\x82\xb4\xe3\x82\xb7\xe3\x83\x83\xe3\x82\xaf W3.ttc"))},
				// Chinese fallback
				{fonts.getChildFile("Hiragino Sans GB.ttc"), fonts.getChildFile("STHeiti Light.ttc")},
				// Hangul
				{fonts.getChildFile("AppleSDGothicNeo.ttc")},
				// wide coverage; moved to Supplemental in macOS 10.15
				{fonts.getChildFile("Supplemental/Arial Unicode.ttf"), juce::File("/Library/Fonts/Arial Unicode.ttf")},
			};
#elif JUCE_WINDOWS
			const auto fonts = juce::File::getSpecialLocation(juce::File::windowsSystemDirectory).getSiblingFile("Fonts");
			return {
				{fonts.getChildFile("YuGothR.ttc"), fonts.getChildFile("meiryo.ttc"), fonts.getChildFile("msgothic.ttc")},
				{fonts.getChildFile("msyh.ttc"), fonts.getChildFile("msyh.ttf"), fonts.getChildFile("simsun.ttc")},
				{fonts.getChildFile("msjh.ttc"), fonts.getChildFile("msjh.ttf"), fonts.getChildFile("mingliu.ttc")},
				{fonts.getChildFile("malgun.ttf"), fonts.getChildFile("gulim.ttc")},
				// wide coverage, installed with Office
				{fonts.getChildFile("arialuni.ttf")},
			};
#else
			// Distributions keep fonts in different places, so they are looked up by file name.
			std::map<juce::String, juce::File> installed;
			const auto home = juce::File::getSpecialLocation(juce::File::userHomeDirectory);
			for (const auto& root : {juce::File("/usr/share/fonts"), juce::File("/usr/local/share/fonts"),
			                         home.getChildFile(".local/share/fonts"), home.getChildFile(".fonts")})
			{
				if (!root.isDirectory())
					continue;
				for (const auto& file : root.findChildFiles(juce::File::findFiles, true, "*.ttf;*.ttc;*.otf"))
					installed.emplace(file.getFileName().toLowerCase(), file);
			}
			const auto group = [&installed](std::initializer_list<const char*> _names)
			{
				Group result;
				for (const auto* name : _names)
					if (const auto it = installed.find(juce::String(name).toLowerCase()); it != installed.end())
						result.push_back(it->second);
				return result;
			};
			return {
				group({"NotoSansCJK-Regular.ttc", "NotoSansCJKjp-Regular.otf", "NotoSansJP-Regular.otf", "ipagp.ttf", "VL-PGothic-Regular.ttf"}),
				group({"DroidSansFallbackFull.ttf", "DroidSansFallback.ttf", "wqy-microhei.ttc", "wqy-zenhei.ttc"}),
				group({"NanumGothic.ttf", "UnDotum.ttf"}),
				group({"DejaVuSans.ttf", "FreeSans.ttf", "unifont.otf", "unifont.ttf"}),
			};
#endif
		}

		// Shared by every instance that loads the file, and unmapped once the last of them is gone.
		std::shared_ptr<const juce::MemoryMappedFile> mapFile(const std::string& _path)
		{
			static std::mutex mutex;
			static std::map<std::string, std::weak_ptr<const juce::MemoryMappedFile>> files;

			const std::lock_guard lock(mutex);
			auto file = files[_path].lock();
			if (!file)
			{
				file = std::make_shared<const juce::MemoryMappedFile>(juce::File(juce::String::fromUTF8(_path.c_str())),
				                                                      juce::MemoryMappedFile::readOnly);
				files[_path] = file;
			}
			return file->getData() && file->getSize() ? file : nullptr;
		}
	}

	std::vector<std::string> findFallbackFontFiles()
	{
		static const std::vector<std::string> files = []
		{
			std::vector<std::string> result;
			for (const auto& group : candidateGroups())
			{
				for (const auto& file : group)
				{
					if (!file.existsAsFile())
						continue;
					result.push_back(file.getFullPathName().toStdString());
					break;
				}
			}
			return result;
		}();
		return files;
	}

	std::vector<std::shared_ptr<const void>> loadFallbackFaces(Rml::CoreInstance& _coreInstance)
	{
		std::vector<std::shared_ptr<const void>> handles;
		for (const auto& path : findFallbackFontFiles())
		{
			auto file = mapFile(path);
			if (!file)
				continue;
			Rml::Log::Message(Rml::Log::LT_INFO, "Loading fallback font face from %s", path.c_str());
			const Rml::Span<const Rml::byte> data(static_cast<const Rml::byte*>(file->getData()), file->getSize());
			// An empty family makes RmlUi read it from the face, as it does for a file.
			if (Rml::LoadFontFace(_coreInstance, data, {}, Rml::Style::FontStyle::Normal, Rml::Style::FontWeight::Auto, true, 0))
				handles.push_back(std::move(file));
		}
		return handles;
	}
}
