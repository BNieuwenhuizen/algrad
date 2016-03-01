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

lir::Reg
getReg(SelectionContext& ctx, hir::Def& def, lir::RegClass rc, unsigned size)
{
    if (ctx.regMap[def.id()] == ~0U) {
        ctx.regMap[def.id()] = ctx.lprog->allocateId();
    }

    return lir::Reg{ctx.regMap[def.id()], rc, size};
}

void
createStartInstruction(SelectionContext& ctx, lir::Program& lprog, hir::Program& program)
{
    lir::Inst newInst{lir::OpCode::start, static_cast<unsigned>(program.params().size())};
    newInst.args()[0] = lir::Arg{getReg(ctx, *program.params()[0], lir::RegClass::sgpr, 4), lir::Arg::Role::def, lir::PhysReg{16}};
    newInst.args()[1] = lir::Arg{getReg(ctx, *program.params()[1], lir::RegClass::vgpr, 4), lir::Arg::Role::def, lir::PhysReg{0 + 256}};
    newInst.args()[2] = lir::Arg{getReg(ctx, *program.params()[2], lir::RegClass::vgpr, 4), lir::Arg::Role::def, lir::PhysReg{1 + 256}};

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
                    lbb.instructions().emplace_back(lir::OpCode::s_endpgm, 0);
                    break;
                case hir::OpCode::gcnInterpolate: {
                    lir::Inst newInsn1{lir::OpCode::v_interp_p1_f32, 5};
                    lir::Inst newInsn2{lir::OpCode::v_interp_p2_f32, 6};

                    lir::Reg tmp{lprog->allocateId(), lir::RegClass::vgpr, 4};

                    newInsn1.args()[0] = lir::Arg{tmp, lir::Arg::Role::def};
                    newInsn1.args()[1] = lir::Arg{getReg(ctx, *insn.getOperand(1), lir::RegClass::vgpr, 4), lir::Arg::Role::use};
                    newInsn1.args()[2] =
                      lir::Arg::int32Constant(static_cast<hir::ScalarConstant*>(insn.getOperand(3))->integerValue());
                    newInsn1.args()[3] =
                      lir::Arg::int32Constant(static_cast<hir::ScalarConstant*>(insn.getOperand(4))->integerValue());
                    newInsn1.args()[4] =
                      lir::Arg{getReg(ctx, *insn.getOperand(0), lir::RegClass::sgpr, 4), lir::Arg::Role::use, lir::PhysReg{124}};

                    newInsn2.args()[0] = lir::Arg{getReg(ctx, insn, lir::RegClass::vgpr, 4), lir::Arg::Role::def};
                    newInsn2.args()[1] = lir::Arg{tmp, lir::Arg::Role::use};
                    newInsn2.args()[2] = lir::Arg{getReg(ctx, *insn.getOperand(2), lir::RegClass::vgpr, 4), lir::Arg::Role::use};
                    newInsn2.args()[3] =
                      lir::Arg::int32Constant(static_cast<hir::ScalarConstant*>(insn.getOperand(3))->integerValue());
                    newInsn2.args()[4] =
                      lir::Arg::int32Constant(static_cast<hir::ScalarConstant*>(insn.getOperand(4))->integerValue());
                    newInsn2.args()[5] =
                      lir::Arg{getReg(ctx, *insn.getOperand(0), lir::RegClass::sgpr, 4), lir::Arg::Role::use, lir::PhysReg{124}};

                    lbb.instructions().emplace_back(std::move(newInsn2));
                    lbb.instructions().emplace_back(std::move(newInsn1));
                } break;
                case hir::OpCode::gcnExport: {
                    lir::Inst newInsn{lir::OpCode::exp, 9};
                    newInsn.args()[0] =
                      lir::Arg::int32Constant(static_cast<hir::ScalarConstant*>(insn.getOperand(0))->integerValue());
                    newInsn.args()[1] =
                      lir::Arg::int32Constant(static_cast<hir::ScalarConstant*>(insn.getOperand(1))->integerValue());
                    newInsn.args()[2] =
                      lir::Arg::int32Constant(static_cast<hir::ScalarConstant*>(insn.getOperand(2))->integerValue());
                    newInsn.args()[3] = lir::Arg::int32Constant(1);
                    newInsn.args()[4] = lir::Arg::int32Constant(1);
                    newInsn.args()[5] = lir::Arg{getReg(ctx, *insn.getOperand(3), lir::RegClass::vgpr, 4), lir::Arg::Role::use};
                    newInsn.args()[6] = lir::Arg{getReg(ctx, *insn.getOperand(4), lir::RegClass::vgpr, 4), lir::Arg::Role::use};
                    newInsn.args()[7] = lir::Arg{getReg(ctx, *insn.getOperand(5), lir::RegClass::vgpr, 4), lir::Arg::Role::use};
                    newInsn.args()[8] = lir::Arg{getReg(ctx, *insn.getOperand(6), lir::RegClass::vgpr, 4), lir::Arg::Role::use};

                    lbb.instructions().emplace_back(std::move(newInsn));
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
