#include "hir.hpp"
#include "hir_inlines.hpp"

namespace algrad {
namespace compiler {

using namespace hir;

namespace {
void
visit(std::vector<bool>& used, Def& def)
{
    if (used[def.id()])
        return;
    used[def.id()] = true;
    if (def.opCode() != OpCode::constant && def.opCode() != OpCode::function) {
        auto operandCount = static_cast<Inst&>(def).operandCount();
        for (std::size_t i = 0; i < operandCount; ++i) {
            visit(used, *static_cast<Inst&>(def).getOperand(i));
        }
    }
}
}

void
eliminateDeadCode(Program& program)
{
    std::vector<bool> used(program.defIdCount());
    for (auto& bb : program.basicBlocks()) {
        for (auto& insn : bb->instructions()) {
            if (!!(insn->flags() & (InstFlags::hasSideEffects | InstFlags::isControlInstruction)))
                visit(used, *insn);
        }
    }
    for (auto& bb : program.basicBlocks()) {
        auto& insns = bb->instructions();
        insns.erase(std::remove_if(insns.begin(), insns.end(), [&used](auto& insn) { return !used[insn->id()]; }),
                    insns.end());
    }
}
}
}
