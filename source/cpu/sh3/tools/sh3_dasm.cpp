// Disassemble a range of a big-endian SH-3 image.
//
//   sh3_dasm <image> <image_base> [start [length]]     (addresses in hex)
//
// `image_base` is the address of the image's first byte; `start` and
// `length` default to the whole image.  Every line shows the address, the
// instruction word and its disassembly; words in the range of a MOV.L
// literal pool still disassemble, so read them with the referencing
// instruction in mind.
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "cpu/sh3/disasm.hpp"

int main(int argc, char** argv) {
  if (argc < 3) {
    std::fprintf(stderr, "usage: %s <image> <image_base_hex> [start_hex [length_hex]]\n", argv[0]);
    return 2;
  }
  std::FILE* f = std::fopen(argv[1], "rb");
  if (!f) {
    std::perror(argv[1]);
    return 1;
  }
  std::vector<unsigned char> image;
  unsigned char buf[65536];
  for (size_t n; (n = std::fread(buf, 1, sizeof buf, f)) > 0;) image.insert(image.end(), buf, buf + n);
  std::fclose(f);

  const unsigned long base = std::strtoul(argv[2], nullptr, 16);
  unsigned long start = argc > 3 ? std::strtoul(argv[3], nullptr, 16) : base;
  unsigned long length = argc > 4 ? std::strtoul(argv[4], nullptr, 16) : image.size() - (start - base);
  if (start < base || start - base >= image.size()) {
    std::fprintf(stderr, "start outside the image\n");
    return 1;
  }
  if (start - base + length > image.size()) length = image.size() - (start - base);

  for (unsigned long a = start & ~1ul; a + 1 < start + length; a += 2) {
    const size_t off = a - base;
    const unsigned code = (unsigned(image[off]) << 8) | image[off + 1];
    std::printf("%08lX: %04X  %s\n", a, code, sh3::disassemble(sh3::u16(code), sh3::u32(a)).c_str());
  }
  return 0;
}
