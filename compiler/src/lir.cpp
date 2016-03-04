#include "lir.hpp"

#include <algorithm>

namespace algrad {
namespace compiler {
namespace lir {

Program::Program()
  : nextId_{0}
{
}

char const*
toString(OpCode op)
{
    switch (op) {
#define HANDLE(v)                                                                                                      \
    case OpCode::v:                                                                                                    \
        return #v;
        ALGRAD_COMPILER_LIR_OPCODES(HANDLE)
#undef HANDLE
    }
}

void
print(std::ostream& os, Program& program)
{
    os << "----- lprogram -----\n";
    for (auto& bb : program.blocks()) {
        os << "    block\n";
        for (auto& insn : bb->instructions()) {
            os << "      " << toString(insn->opCode());
            os << "\n";
        }
    }
}
}
}
}
