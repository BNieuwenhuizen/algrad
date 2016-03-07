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
    if (def.opCode() != OpCode::constant) {
        auto operandCount = static_cast<Inst&>(def).operandCount();
        for (std::size_t i = 0; i < operandCount; ++i) {
            visit(used, *static_cast<Inst&>(def).getOperand(i));
        }
    }
}

void
eliminate(std::vector<bool> const& used, std::vector<std::unique_ptr<Inst>>& insts)
{
    insts.erase(std::remove_if(insts.begin(), insts.end(), [&used](auto& insn) { return !used[insn->id()]; }),
                insts.end());
}

void
eliminate(std::vector<bool> const& used, BasicBlock& bb)
{
    auto insts = bb.instructions();
    for (auto it = insts.begin(); it != insts.end();) {
        auto& insn = *it++;
        if (!used[insn.id()])
            bb.erase(insn);
    }
}

void
eliminateVars(std::vector<bool> const& used, Program& program)
{
    auto insts = program.variables();
    for (auto it = insts.begin(); it != insts.end();) {
        auto& insn = *it++;
        if (!used[insn.id()])
            program.eraseVariable(insn);
    }
}
}

void
eliminateDeadCode(Program& program)
{
    std::vector<bool> used(program.defIdCount());
    for (auto& bb : program.basicBlocks()) {
        for (auto& insn : bb->instructions()) {
            if (!!(insn.flags() & (InstFlags::hasSideEffects | InstFlags::isControlInstruction)))
                visit(used, insn);
        }
    }
    for (auto& bb : program.basicBlocks()) {
        eliminate(used, *bb);
    }
    eliminateVars(used, program);
    eliminate(used, program.params());
}
}
}
