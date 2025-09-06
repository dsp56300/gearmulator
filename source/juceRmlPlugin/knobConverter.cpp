#include <fstream>

#include "juce_graphics/juce_graphics.h"

#include "juceRmlPlugin/skinConverter/skinConverter.h"

int main(int _argc, char** _argv)
{
	if (_argc < 2)
	{
		printf("Usage: knobConverter <image>\n");
		return 1;
	}
	const auto imageName = _argv[1];

	// Load image via Juce
	const juce::File file = juce::File::getCurrentWorkingDirectory().getChildFile(imageName);

	if (!file.existsAsFile())
	{
		printf("File does not exist: %s\n", imageName);
		return 1;
	}

	auto image = juce::ImageFileFormat::loadFrom(file);

	// tile size is defined by the smaller dimension
	const auto tileSize = std::min(image.getWidth(), image.getHeight());

	const auto name = file.getFileNameWithoutExtension().toStdString();

	auto spriteSheets = rmlPlugin::skinConverter::SkinConverter::createSpritesheet(
		juce::File::getCurrentWorkingDirectory().getFullPathName().toStdString() + '/', 
		2048, 2048, tileSize, tileSize, name, image, {});

	if (spriteSheets.empty())
	{
		printf("Failed to create spritesheet\n");
		return 1;
	}

	std::ofstream out(name + ".rcss");

	if (!out.is_open())
	{
		printf("Failed to open output file for writing\n");
		return 1;
	}
	for (auto& [key, spritesheet] : spriteSheets)
	{
		spritesheet.write(out, key, 0);
		out << '\n';
	}
	return 0;
}