// ppcdis <default.xex> <hex address> <count>
// Disassembles guest code from the loaded image with XenonRecomp's own disassembler (what the
// recompiler sees), one instruction a line: address, raw word, mnemonic, operands.
#include <cstdint>
#include <cstdio>
#include <cstdlib>

#include <byteswap.h>
#include <disasm.h>
#include <file.h>
#include <image.h>

int main(int argc, char** argv)
{
    if (argc < 4)
    {
        std::fprintf(stderr, "usage: ppcdis <default.xex> <hex address> <count>\n");
        return 1;
    }
    const auto file = LoadFile(argv[1]);
    Image image = Image::ParseImage(file.data(), file.size());
    uint32_t addr = std::strtoul(argv[2], nullptr, 16);
    const int count = std::atoi(argv[3]);
    for (int i = 0; i < count; i++, addr += 4)
    {
        const auto* code = reinterpret_cast<const uint32_t*>(image.Find(addr));
        if (!code)
        {
            std::printf("%08X  (outside the image)\n", addr);
            break;
        }
        ppc_insn insn;
        ppc::Disassemble(code, addr, insn);
        std::printf("%08X  %08X  %s %s\n", addr, ByteSwap(*code), insn.opcode ? insn.opcode->name : "?", insn.op_str);
    }
    return 0;
}
