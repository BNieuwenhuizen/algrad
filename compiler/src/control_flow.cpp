#include "hir.hpp"
#include "hir_inlines.hpp"

#include <algorithm>
#include <queue>

namespace algrad {
namespace compiler {

namespace {
void
visitRPO(hir::BasicBlock& bb, int& index)
{
    if (bb.id() >= 0 || bb.id() == -2)
        return;
    bb.setId(-2);
    for (auto succ : bb.successors())
        visitRPO(*succ, index);

    bb.setId(--index);
}
}

void
orderBlocksRPO(hir::Program& program)
{
    int index = program.basicBlocks().size();
    for (auto& bb : program.basicBlocks())
        bb->setId(-1);

    for (auto& bb : program.basicBlocks())
        visitRPO(*bb, index);

    std::sort(program.basicBlocks().begin(), program.basicBlocks().end(),
              [](auto& a, auto& b) { return a->id() < b->id(); });
}
namespace {
void
markVarying(hir::Inst& inst)
{
    if (inst.isVarying() || !!(inst.flags() & hir::InstFlags::alwaysUniform))
        return;

    inst.markVarying();

    for (auto& u : inst.uses()) {
        markVarying(*u.consumer());
    }
}
}

void
determineDivergence(hir::Program& program)
{
    for (auto& p : program.params()) {
        if (!!(p->flags() & hir::InstFlags::alwaysVarying))
            markVarying(*p);
    }
    for (auto& bb : program.basicBlocks()) {
        for (auto& inst : bb->instructions()) {
            if (!!(inst.flags() & hir::InstFlags::alwaysVarying) || inst.opCode() == hir::OpCode::phi)
                markVarying(inst);
        }
    }
}
}
}
