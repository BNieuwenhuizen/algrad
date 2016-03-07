#include "hir.hpp"
#include "hir_inlines.hpp"

#include <algorithm>

namespace algrad {
namespace compiler {

namespace {
void
visitRPO(hir::BasicBlock& bb, int& index)
{
    if (bb.id() >= 0)
        return;
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
}
}
