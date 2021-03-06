#include "hir.hpp"
#include "hir_inlines.hpp"

namespace algrad {
namespace compiler {

using namespace hir;

void
lowerInput(Program& program)
{
    std::vector<std::unique_ptr<Inst>> params, params2;
    ;
    params.push_back(program.createDef<Inst>(OpCode::parameter, &int32Type, 0));
    params.push_back(program.createDef<Inst>(OpCode::parameter, &float32Type, hir::InstFlags::alwaysVarying, 0));
    params.push_back(program.createDef<Inst>(OpCode::parameter, &float32Type, hir::InstFlags::alwaysVarying, 0));
    std::swap(params, program.params());

    int idx = 0;
    for (auto& p : params) {
        auto p2 = program.createDef<Inst>(OpCode::gcnInterpolate, &float32Type, 5);
        for (int i = 0; i < 3; ++i)
            p2->setOperand(i, program.params()[i].get());
        p2->setOperand(3, program.getScalarConstant(&int32Type, static_cast<std::uint64_t>(idx / 4)));
        p2->setOperand(4, program.getScalarConstant(&int32Type, static_cast<std::uint64_t>(idx % 4)));
        ++idx;

        replace(*p, *p2);
        params2.push_back(std::move(p2));
    }

    for (auto it = params2.rbegin(); it != params2.rend(); ++it) {
        program.initialBlock().insertFront(std::move(*it));
    }
}

hir::BasicBlock& find_ret_block(Program& program) {
	for(auto& block : program.basicBlocks()) {
		if(block->instructions().empty())
			continue;
		auto& inst = block->instructions().back();
		if(inst.opCode() == hir::OpCode::ret)
			return *block;
	}
	std::abort();
}

void
lowerOutput(Program& program)
{
    auto& endBlock = find_ret_block(program);
    auto& ret = endBlock.instructions().back();
    if (ret.operandCount() == 0)
        std::abort();

    endBlock.instructions().pop_back();
    if (ret.opCode() != OpCode::ret)
        std::terminate();

    if (ret.operandCount() & 3)
        std::terminate();

    auto operandCount = ret.operandCount();
    for (int i = 0; i + 4 <= operandCount; ++i) {
        auto& exportInsn = endBlock.insertBack(program.createDef<Inst>(OpCode::gcnExport, &voidType, 7));
        exportInsn.setOperand(0, program.getScalarConstant(&int32Type, static_cast<std::uint64_t>(15)));
        exportInsn.setOperand(1, program.getScalarConstant(&int32Type, static_cast<std::uint64_t>(i / 4)));
        exportInsn.setOperand(2, program.getScalarConstant(&int32Type, static_cast<std::uint64_t>(0)));
        for (int j = 0; j < 4; ++j)
            exportInsn.setOperand(3 + j, ret.getOperand(i + j));
    }

    endBlock.erase(ret);
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
