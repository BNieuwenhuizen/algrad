#include "ir.hpp"

namespace algrad {
namespace compiler {

namespace {
void
visit(std::vector<bool>& used, Def& def)
{
    if (used[def.id()])
        return;
    used[def.id()] = true;
    if (def.opCode() != OpCode::constant && def.opCode() != OpCode::function) {
        for (auto op : static_cast<Instruction&>(def).operands())
            visit(used, *op);
    }
}

bool
hasSideEffects(Instruction& insn)
{
    switch (insn.opCode()) {
        case OpCode::store:
        case OpCode::ret:
            return true;
        default:
            return false;
    }
}
}

void
eliminateDeadCode(Program& program)
{
    std::vector<bool> used(program.defIdCount());
    for (auto& fun : program.functions()) {
        for (auto& bb : fun->basicBlocks()) {
            for (auto& insn : bb->instructions()) {
                if (hasSideEffects(*insn))
                    visit(used, *insn);
            }
        }
        for (auto& bb : fun->basicBlocks()) {
            auto& insns = bb->instructions();
            insns.erase(std::remove_if(insns.begin(), insns.end(), [&used](auto& insn) { return !used[insn->id()]; }),
                        insns.end());
        }
    }
}
}
}