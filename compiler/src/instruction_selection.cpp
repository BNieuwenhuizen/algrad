#include "hir.hpp"
#include "hir_inlines.hpp"
#include "lir.hpp"
#include <iostream>

namespace algrad {
namespace compiler {

using namespace hir;

std::vector<RegClass>
computeRegisterClasses(Program& program)
{
    std::vector<RegClass> regClasses(program.defIdCount(), RegClass::sgpr);
    regClasses[program.params()[0]->id()] = RegClass::sgpr;
    regClasses[program.params()[1]->id()] = RegClass::vgpr;
    regClasses[program.params()[2]->id()] = RegClass::vgpr;
    for (auto& bb : program.basicBlocks()) {
        for (auto& insn : bb->instructions()) {
            if (insn->type() == &voidType)
                continue;

            switch (insn->opCode()) {
                default: {
                    auto operandCount = insn->operandCount();
                    for (std::size_t i = 0; i < operandCount; ++i) {
                        if (regClasses[insn->getOperand(i)->id()] == RegClass::vgpr)
                            regClasses[insn->id()] = RegClass::vgpr;
                    }
                }
            }
        }
    }
    return regClasses;
}

struct SelectionContext
{
    std::vector<RegClass> regClasses;
    std::vector<std::uint32_t> regMap;
    LProgram* lprog;
};

LReg
getReg(SelectionContext& ctx, Def& def, RegClass rc, unsigned size)
{
    if (ctx.regMap[def.id()] == ~0U) {
        ctx.regMap[def.id()] = ctx.lprog->allocateId();
    }

    return LReg{ctx.regMap[def.id()], rc, size};
}

void
createStartInstruction(SelectionContext& ctx, LFunction& lfunc, hir::Program& program)
{
    LInst newInst{LOpCode::start, static_cast<unsigned>(program.params().size())};
    newInst.args()[0] = LArg{getReg(ctx, *program.params()[0], RegClass::sgpr, 4), LArg::Role::def, PhysReg{16}};
    newInst.args()[1] = LArg{getReg(ctx, *program.params()[1], RegClass::vgpr, 4), LArg::Role::def, PhysReg{0 + 256}};
    newInst.args()[2] = LArg{getReg(ctx, *program.params()[2], RegClass::vgpr, 4), LArg::Role::def, PhysReg{1 + 256}};

    lfunc.blocks().front()->instructions().push_back(std::move(newInst));
}

std::unique_ptr<LProgram>
selectInstructions(Program& program)
{
    SelectionContext ctx;
    ctx.regClasses = computeRegisterClasses(program);
    ctx.regMap.resize(program.defIdCount(), ~0U);

    auto lprog = std::make_unique<LProgram>();
    ctx.lprog = lprog.get();

    lprog->functions().push_back(std::make_unique<LFunction>());
    auto& lfunc = *lprog->functions().back();

    for (auto& bb : program.basicBlocks()) {
        lfunc.blocks().push_back(std::make_unique<LBlock>());
    }

    for (int i = program.basicBlocks().size() - 1; i >= 0; --i) {
        auto& bb = *program.basicBlocks()[i];
        auto& lbb = *lfunc.blocks()[i];

        for (int j = bb.instructions().size() - 1; j >= 0; --j) {
            Inst& insn = *bb.instructions()[j];
            switch (insn.opCode()) {
                case OpCode::ret:
                    lbb.instructions().emplace_back(LOpCode::s_endpgm, 0);
                    break;
                case OpCode::gcnInterpolate: {
                    LInst newInsn1{LOpCode::v_interp_p1_f32, 5};
                    LInst newInsn2{LOpCode::v_interp_p2_f32, 6};

                    LReg tmp{lprog->allocateId(), RegClass::vgpr, 4};

                    newInsn1.args()[0] = LArg{tmp, LArg::Role::def};
                    newInsn1.args()[1] = LArg{getReg(ctx, *insn.getOperand(1), RegClass::vgpr, 4), LArg::Role::use};
                    newInsn1.args()[2] =
                      LArg::int32Constant(static_cast<ScalarConstant*>(insn.getOperand(3))->integerValue());
                    newInsn1.args()[3] =
                      LArg::int32Constant(static_cast<ScalarConstant*>(insn.getOperand(4))->integerValue());
                    newInsn1.args()[4] =
                      LArg{getReg(ctx, *insn.getOperand(0), RegClass::sgpr, 4), LArg::Role::use, PhysReg{124}};

                    newInsn2.args()[0] = LArg{getReg(ctx, insn, RegClass::vgpr, 4), LArg::Role::def};
                    newInsn2.args()[1] = LArg{tmp, LArg::Role::use};
                    newInsn2.args()[2] = LArg{getReg(ctx, *insn.getOperand(2), RegClass::vgpr, 4), LArg::Role::use};
                    newInsn2.args()[3] =
                      LArg::int32Constant(static_cast<ScalarConstant*>(insn.getOperand(3))->integerValue());
                    newInsn2.args()[4] =
                      LArg::int32Constant(static_cast<ScalarConstant*>(insn.getOperand(4))->integerValue());
                    newInsn2.args()[5] =
                      LArg{getReg(ctx, *insn.getOperand(0), RegClass::sgpr, 4), LArg::Role::use, PhysReg{124}};

                    lbb.instructions().emplace_back(std::move(newInsn2));
                    lbb.instructions().emplace_back(std::move(newInsn1));
                } break;
                case OpCode::gcnExport: {
                    LInst newInsn{LOpCode::exp, 9};
                    newInsn.args()[0] =
                      LArg::int32Constant(static_cast<ScalarConstant*>(insn.getOperand(0))->integerValue());
                    newInsn.args()[1] =
                      LArg::int32Constant(static_cast<ScalarConstant*>(insn.getOperand(1))->integerValue());
                    newInsn.args()[2] =
                      LArg::int32Constant(static_cast<ScalarConstant*>(insn.getOperand(2))->integerValue());
                    newInsn.args()[3] = LArg::int32Constant(1);
                    newInsn.args()[4] = LArg::int32Constant(1);
                    newInsn.args()[5] = LArg{getReg(ctx, *insn.getOperand(3), RegClass::vgpr, 4), LArg::Role::use};
                    newInsn.args()[6] = LArg{getReg(ctx, *insn.getOperand(4), RegClass::vgpr, 4), LArg::Role::use};
                    newInsn.args()[7] = LArg{getReg(ctx, *insn.getOperand(5), RegClass::vgpr, 4), LArg::Role::use};
                    newInsn.args()[8] = LArg{getReg(ctx, *insn.getOperand(6), RegClass::vgpr, 4), LArg::Role::use};

                    lbb.instructions().emplace_back(std::move(newInsn));
                } break;
                default:
                    std::terminate();
            }
        }
    }

    createStartInstruction(ctx, lfunc, program);

    for (auto& b : lfunc.blocks())
        std::reverse(b->instructions().begin(), b->instructions().end());
    return lprog;
}
}
}
