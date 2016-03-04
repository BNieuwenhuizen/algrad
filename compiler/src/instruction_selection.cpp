#include "hir.hpp"
#include "hir_inlines.hpp"
#include "lir.hpp"
#include <iostream>

namespace algrad {
namespace compiler {

std::vector<lir::RegClass>
computeRegisterClasses(hir::Program& program)
{
    std::vector<lir::RegClass> regClasses(program.defIdCount(), lir::RegClass::sgpr);
    regClasses[program.params()[0]->id()] = lir::RegClass::sgpr;
    regClasses[program.params()[1]->id()] = lir::RegClass::vgpr;
    regClasses[program.params()[2]->id()] = lir::RegClass::vgpr;
    for (auto& bb : program.basicBlocks()) {
        for (auto& insn : bb->instructions()) {
            if (insn->type() == &voidType)
                continue;

            switch (insn->opCode()) {
                default: {
                    auto operandCount = insn->operandCount();
                    for (std::size_t i = 0; i < operandCount; ++i) {
                        if (regClasses[insn->getOperand(i)->id()] == lir::RegClass::vgpr)
                            regClasses[insn->id()] = lir::RegClass::vgpr;
                    }
                }
            }
        }
    }
    return regClasses;
}

struct SelectionContext
{
    std::vector<lir::RegClass> regClasses;
    std::vector<std::uint32_t> regMap;
    lir::Program* lprog;
};

lir::Temp
getReg(SelectionContext& ctx, hir::Def& def, lir::RegClass rc, unsigned size)
{
    if (ctx.regMap[def.id()] == ~0U) {
        ctx.regMap[def.id()] = ctx.lprog->allocateId();
    }

    return lir::Temp{ctx.regMap[def.id()], rc, size};
}

lir::Temp
getSingleSGPR(SelectionContext& ctx, hir::Def& def)
{
    return getReg(ctx, def, lir::RegClass::sgpr, 4);
}

lir::Temp
getSingleVGPR(SelectionContext& ctx, hir::Def& def)
{
    return getReg(ctx, def, lir::RegClass::vgpr, 4);
}

void
createStartInstruction(SelectionContext& ctx, lir::Program& lprog, hir::Program& program)
{
    auto newInst = std::make_unique<lir::StartInst>(3);
    newInst->getDefinition(0) = lir::Def{getReg(ctx, *program.params()[0], lir::RegClass::sgpr, 4), lir::PhysReg{16}};
    newInst->getDefinition(1) = lir::Def{getSingleVGPR(ctx, *program.params()[1]), lir::PhysReg{0 + 256}};
    newInst->getDefinition(2) = lir::Def{getSingleVGPR(ctx, *program.params()[2]), lir::PhysReg{1 + 256}};

    lprog.blocks().front()->instructions().push_back(std::move(newInst));
}

std::unique_ptr<lir::Program>
selectInstructions(hir::Program& program)
{
    SelectionContext ctx;
    ctx.regClasses = computeRegisterClasses(program);
    ctx.regMap.resize(program.defIdCount(), ~0U);

    auto lprog = std::make_unique<lir::Program>();
    ctx.lprog = lprog.get();

    for (auto& bb : program.basicBlocks()) {
        ctx.lprog->blocks().push_back(std::make_unique<lir::Block>());
    }

    for (int i = program.basicBlocks().size() - 1; i >= 0; --i) {
        auto& bb = *program.basicBlocks()[i];
        auto& lbb = *ctx.lprog->blocks()[i];

        for (int j = bb.instructions().size() - 1; j >= 0; --j) {
            hir::Inst& insn = *bb.instructions()[j];
            switch (insn.opCode()) {
                case hir::OpCode::ret:
                    lbb.instructions().push_back(std::make_unique<lir::EndProgramInstruction>());
                    break;
                case hir::OpCode::gcnInterpolate: {
                    auto attribute = static_cast<hir::ScalarConstant*>(insn.getOperand(3))->integerValue();
                    auto component = static_cast<hir::ScalarConstant*>(insn.getOperand(4))->integerValue();
                    auto p1 = std::make_unique<lir::VInterpP1F32Inst>(attribute, component);
                    auto p2 = std::make_unique<lir::VInterpP2F32Inst>(attribute, component);

                    lir::Temp tmp{lprog->allocateId(), lir::RegClass::vgpr, 4};
                    p1->getDefinition(0) = lir::Def{tmp};
                    p1->getOperand(0) = lir::Operand{getSingleVGPR(ctx, *insn.getOperand(1))};
                    p1->getOperand(1) = lir::Operand{getSingleSGPR(ctx, *insn.getOperand(0)), lir::PhysReg{124}};

                    p2->getDefinition(0) = lir::Def{getReg(ctx, insn, lir::RegClass::vgpr, 4)};
                    p2->getOperand(0) = lir::Operand{tmp};
                    p2->getOperand(1) = lir::Operand{getSingleVGPR(ctx, *insn.getOperand(2))};
                    p2->getOperand(2) = lir::Operand{getSingleSGPR(ctx, *insn.getOperand(0)), lir::PhysReg{124}};

                    lbb.instructions().emplace_back(std::move(p1));
                    lbb.instructions().emplace_back(std::move(p2));
                } break;
                case hir::OpCode::gcnExport: {
                    auto enabledMask = static_cast<hir::ScalarConstant*>(insn.getOperand(0))->integerValue();
                    auto dest = static_cast<hir::ScalarConstant*>(insn.getOperand(1))->integerValue();
                    auto compressed = static_cast<hir::ScalarConstant*>(insn.getOperand(2))->integerValue();
                    auto exp = std::make_unique<lir::ExportInst>(enabledMask, dest, compressed, true, true);

                    exp->getOperand(0) = lir::Operand{getSingleVGPR(ctx, *insn.getOperand(3))};
                    exp->getOperand(1) = lir::Operand{getSingleVGPR(ctx, *insn.getOperand(4))};
                    exp->getOperand(2) = lir::Operand{getSingleVGPR(ctx, *insn.getOperand(5))};
                    exp->getOperand(3) = lir::Operand{getSingleVGPR(ctx, *insn.getOperand(6))};

                    lbb.instructions().emplace_back(std::move(exp));
                } break;
                default:
                    std::terminate();
            }
        }
    }

    createStartInstruction(ctx, *ctx.lprog, program);

    for (auto& b : ctx.lprog->blocks())
        std::reverse(b->instructions().begin(), b->instructions().end());
    return lprog;
}
}
}
