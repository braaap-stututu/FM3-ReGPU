// xexdump <default.xex> <out.bin>
// Writes the loaded image (from the image base, decrypted and decompressed) to <out.bin> and
// prints the base, size, entry point and section list. Uses XenonUtils' XEX loader, so the
// result is exactly what XenonRecomp/XenonAnalyse see.
#include <cstdio>
#include <cstring>
#include <string>
#include <fstream>

#include <file.h>
#include <image.h>

int main(int argc, char** argv)
{
    if (argc < 3)
    {
        std::fprintf(stderr, "usage: xexdump <default.xex> <out.bin>\n");
        return 1;
    }
    const auto file = LoadFile(argv[1]);
    if (file.empty())
    {
        std::fprintf(stderr, "cannot read %s\n", argv[1]);
        return 1;
    }
    Image image = Image::ParseImage(file.data(), file.size());
    if (!image.data)
    {
        std::fprintf(stderr, "%s: not a XEX the loader understands\n", argv[1]);
        return 1;
    }
    std::printf("base %08zX size %08X entry %08zX\n", image.base, image.size, image.entry_point);
    for (const auto& s : image.sections)
    {
        // PE section names are 8 bytes, with no NUL when all 8 are used; XenonUtils reads them as
        // C strings, so a full-length name runs into the next field. Cut it at 8.
        const std::string name = s.name.substr(0, strnlen(s.name.c_str(), 8));
        std::printf("%-8s %08zX %08X %s\n", name.c_str(), s.base, s.size, (s.flags & SectionFlags_Code) ? "code" : "data");
    }

    std::ofstream out(argv[2], std::ios::binary);
    out.write(reinterpret_cast<const char*>(image.data.get()), image.size);
    std::printf("wrote %X bytes to %s: %s\n", image.size, argv[2], out.good() ? "ok" : "FAILED");
    return out.good() ? 0 : 1;
}
