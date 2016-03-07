#include "hir.hpp"
#include "lir.hpp"
#include "spirv_loader.cpp"

#include <fstream>

int
main(int argc, char* argv[])
{
    if (argc != 2)
        throw - 1;
    std::ifstream in(argv[1]);
    if (!in.is_open())
        throw - 1;
    in.seekg(0, std::ios::end);
    std::vector<std::uint32_t> data(in.tellg() / 4);
    in.seekg(0, std::ios::beg);
    in.read(static_cast<char*>(static_cast<void*>(data.data())), data.size() * 4);
    auto prog = algrad::compiler::loadSPIRV(data.data(), data.data() + data.size(), "main");
    algrad::compiler::orderBlocksRPO(*prog);
    algrad::compiler::splitComposites(*prog);
    algrad::compiler::promoteVariables(*prog);
    algrad::compiler::eliminateDeadCode(*prog);
    algrad::compiler::lowerIO(*prog);
    algrad::compiler::determineDivergence(*prog);
    print(std::cout, *prog);

    auto lprog = algrad::compiler::selectInstructions(*prog);
    algrad::compiler::allocateRegisters(*lprog);
    print(std::cout, *lprog);
}
