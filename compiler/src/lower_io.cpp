#include "hir.hpp"
#include "hir_inlines.hpp"

namespace algrad {
namespace compiler {

using namespace hir;

void
lowerInput(Program& program)
{
    std::vector<std::unique_ptr<Inst>> params;
    params.push_back(program.createDef<Inst>(OpCode::parameter, &int32Type, 0));
    params.push_back(program.createDef<Inst>(OpCode::parameter, &float32Type, 0));
    params.push_back(program.createDef<Inst>(OpCode::parameter, &float32Type, 0));
    std::swap(params, program.params());

    int idx = 0;
    for (auto& p : params) {
        *p = Inst{OpCode::gcnInterpolate, p->id(), &float32Type, 5};
        for (int i = 0; i < 3; ++i)
            p->operands()[i] = program.params()[i].get();
        p->operands()[3] = program.getScalarConstant(&int32Type, static_cast<std::uint64_t>(idx / 4));
        p->operands()[4] = program.getScalarConstant(&int32Type, static_cast<std::uint64_t>(idx % 4));
        ++idx;
    }

    auto& insns = program.initialBlock().instructions();
    insns.insert(insns.begin(), make_move_iterator(params.begin()), make_move_iterator(params.end()));
}

void
lowerOutput(Program& program)
{
    auto& endBlock = *program.basicBlocks().back();
    if (endBlock.instructions().back()->operands().empty())
        std::abort();
    auto ret = std::move(endBlock.instructions().back());
    endBlock.instructions().pop_back();
    if (ret->opCode() != OpCode::ret)
        std::terminate();

    if (ret->operands().size() & 3)
        std::terminate();

    for (int i = 0; i + 4 <= ret->operands().size(); ++i) {
        auto& exportInsn = endBlock.insertBack(program.createDef<Inst>(OpCode::gcnExport, &voidType, 7));
        exportInsn.operands()[0] = program.getScalarConstant(&int32Type, static_cast<std::uint64_t>(15));
        exportInsn.operands()[1] = program.getScalarConstant(&int32Type, static_cast<std::uint64_t>(i / 4));
        exportInsn.operands()[2] = program.getScalarConstant(&int32Type, static_cast<std::uint64_t>(0));
        for (int j = 0; j < 4; ++j)
            exportInsn.operands()[3 + j] = ret->operands()[i + j];
    }

    endBlock.insertBack(program.createDef<Inst>(OpCode::ret, &voidType, 0));
}

void
lowerIO(Program& program)
{
    lowerInput(program);
    lowerOutput(program);
}
}
}