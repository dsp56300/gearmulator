#include "romloader.h"

#include "baseLib/filesystem.h"

#include <stdio.h>
#include <string.h>

static int getInt(FILE *f)
{
	int r = 0;
	for (int i = 0; i < 4; i++)
		r = (r << 8) | fgetc(f);
	return r;
}
static int getWord(FILE *f)
{
	int r = fgetc(f);
	return (r << 8) | fgetc(f);
}
static int getVar(FILE *f)
{
	int r = 0, c = 0x80;
	while (!feof(f) && (c & 0x80))
	{
		c = fgetc(f);
		r = (r << 7) | (c & 0x7f);
	}
	return r;
}

static void parseTrack(FILE *f, std::vector<uint8_t> &out)
{
	while (!feof(f))
	{
		int delay = getVar(f);
		int sort = fgetc(f);
		switch (sort)
		{
		case 0xFF:
		{
			int type = fgetc(f);
			int len = fgetc(f);
			if (type == 0x2f)
				return;
			printf("Meta-event (%02x, %d): ", type, len);
			if (type == 1)
				for (int i = 0; i < len; i++)
					printf("%c", fgetc(f));
			else
				for (int i = 0; i < len; i++)
					printf("%02x ", fgetc(f));
			printf("\n");
		}
		break;
		case 0xF0:
		{
			int len = getVar(f);
			printf("%04x: ", len);
			int mfr = fgetc(f);
			int zero1 = fgetc(f);
			int zero2 = fgetc(f);
			int six = fgetc(f);
			int twelve = fgetc(f);
			int offset = getInt(f);
			int checksum = ((offset >> 24) & 255) + ((offset >> 16) & 255) + ((offset >> 8) & 255) + (offset & 255);
			offset =
				((offset & 0x7F000000) >> 3) | ((offset & 0x7F0000) >> 2) | ((offset & 0x7F00) >> 1) | (offset & 0x7F);
			if (mfr != 0x41)
				printf("mfr = %02x, ", mfr);
			if (zero1 != 0)
				printf("z1 = %02x, ", zero1);
			if (zero2 != 0)
				printf("z1 = %02x, ", zero2);
			if (six != 6)
				printf("six = %02x, ", six);
			if (twelve != 0x12)
				printf("twelve = %02x, ", twelve);

			len -= 11;
			printf("Sysex %08x (%d)\n", offset, len);
			int c = 0;
			for (int i = 0; i < len; i += 8)
			{
				int topbits = fgetc(f);
				checksum += topbits;
				int todo = len - i - 1;
				if (todo > 7)
					todo = 7;
				int buffer[7];
				for (int j = 0; j < todo; j++)
				{
					buffer[j] = fgetc(f);
					checksum += buffer[j];
				}
				for (int j = 0; j < todo; j++)
				{
					if (topbits & (1 << j))
						buffer[j] |= 0x80;
					out.push_back(buffer[j]);
				}
			}
			int theirchecksum = fgetc(f), ourchecksum = (0x80 - (checksum & 0x7f)) & 0x7f;
			if (ourchecksum != theirchecksum)
				printf("Checksum = %02x (%02x %02x)\n", ourchecksum, theirchecksum, theirchecksum ^ ourchecksum);
			int end = fgetc(f);
			if (end != 0xf7)
				printf("end = %02x\n", end);
		}
		break;
		}
	}
}

namespace jeLib
{
	Rom RomLoader::findROM()
	{
		auto files = synthLib::RomLoader::findFiles(".mid", RomSizeKeyboardMidi, RomSizeKeyboardMidi);
		if (!files.empty() && files.size() == RomCountKeyboardMidi)
			return loadFromMidiFiles_Keyboard(files);

		files = synthLib::RomLoader::findFiles(".bin", Rom::RomSizeKeyboard, Rom::RomSizeKeyboard);
		if (!files.empty())
			return Rom(files.front());

		files = synthLib::RomLoader::findFiles(".bin", Rom::RomSizeRack, Rom::RomSizeRack);
		if (!files.empty())
			return Rom(files.front());
		return {};
	}

	Rom RomLoader::loadFromMidiFiles_Keyboard(std::vector<std::string> &files)
	{
		std::vector<uint8_t> fullRom;

		std::sort(files.begin(), files.end());

		for (int j = 0; j < 8; j++)
		{
			FILE *f = baseLib::filesystem::openFile(files[j], "rb");
			do
			{
				int i = getInt(f);
				int len = getInt(f);

				if (i == 'MThd')
				{
					int format = getWord(f); // 0
					int tracks = getWord(f); // 1
					int divisons = getWord(f); // 0x60
				}
				if (i == 'MTrk')
				{
					parseTrack(f, fullRom);
				}
			} while (!feof(f));
			fclose(f);
		}

		return Rom(fullRom, "From MIDI files");
	}
} // namespace jeLib
