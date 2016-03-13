#include "hir.hpp"
#include "hir_inlines.hpp"
#include "lir.hpp"

#include <iostream>
#include <map>

#include <boost/range/adaptor/reversed.hpp>

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
            if (insn.type() == &voidType)
                continue;

            if (insn.isVarying())
                regClasses[insn.id()] =
                  insn.type()->kind() == TypeKind::boolean ? lir::RegClass::sgpr : lir::RegClass::vgpr;
            else
                regClasses[insn.id()] =
                  insn.type()->kind() == TypeKind::boolean ? lir::RegClass::scc : lir::RegClass::sgpr;

            switch (insn.opCode()) {
                default: {
                    auto operandCount = insn.operandCount();
                    for (std::size_t i = 0; i < operandCount; ++i) {
                        if (regClasses[insn.getOperand(i)->id()] == lir::RegClass::vgpr)
                            regClasses[insn.id()] =
                              insn.type()->kind() == TypeKind::boolean ? lir::RegClass::sgpr : lir::RegClass::vgpr;
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

    std::map<std::pair<lir::Block*, lir::Block*>, unsigned> controlFlowVars;
};

lir::Temp
getReg(SelectionContext& ctx, hir::Def& def, lir::RegClass rc, unsigned size)
{
    if (ctx.regMap[def.id()] == ~0U) {
        ctx.regMap[def.id()] = ctx.lprog->allocateId();
    }

    if (rc != ctx.regClasses[def.id()])
        std::terminate();
    return lir::Temp{ctx.regMap[def.id()], rc, size};
}

lir::Temp
getReg(SelectionContext& ctx, hir::Def& def)
{
    if (ctx.regMap[def.id()] == ~0U) {
        ctx.regMap[def.id()] = ctx.lprog->allocateId();
    }

    return lir::Temp{ctx.regMap[def.id()], ctx.regClasses[def.id()], 4};
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
    auto newInst = std::make_unique<lir::Inst>(lir::OpCode::start, 3, 0);
    newInst->getDefinition(0) = lir::Arg{getReg(ctx, *program.params()[0], lir::RegClass::sgpr, 4), lir::PhysReg{16}};
    newInst->getDefinition(1) = lir::Arg{getSingleVGPR(ctx, *program.params()[1]), lir::PhysReg{0 + 256}};
    newInst->getDefinition(2) = lir::Arg{getSingleVGPR(ctx, *program.params()[2]), lir::PhysReg{1 + 256}};

    lprog.blocks().front()->instructions().push_back(std::move(newInst));
}

void
createVectorCompare(SelectionContext& ctx, lir::OpCode opCode, hir::Inst& inst, lir::Block& lbb)
{
    auto newInst = std::make_unique<lir::Inst>(opCode, 1, 2);
    newInst->getOperand(0) = lir::Arg{getReg(ctx, *inst.getOperand(0))};
    newInst->getOperand(1) = lir::Arg{getReg(ctx, *inst.getOperand(1))};
    newInst->getDefinition(0) = lir::Arg{getReg(ctx, inst), lir::PhysReg{106}};

    lbb.instructions().push_back(std::move(newInst));
}

void
createLogicalCondBranch(SelectionContext& ctx, hir::Inst& inst, lir::Block& lbb)
{
    auto newInst = std::make_unique<lir::Inst>(lir::OpCode::logical_cond_branch, 2, 1);
    newInst->getOperand(0) = lir::Arg{getReg(ctx, *inst.getOperand(0))};

    newInst->getDefinition(0) =
      lir::Arg{lir::Temp{ctx.controlFlowVars.find({&lbb, lbb.logicalSuccessors()[0]})->second, lir::RegClass::sgpr, 8}};
    newInst->getDefinition(1) =
      lir::Arg{lir::Temp{ctx.controlFlowVars.find({&lbb, lbb.logicalSuccessors()[1]})->second, lir::RegClass::sgpr, 8}};
    lbb.instructions().push_back(std::move(newInst));
}

void
createLogicalBranch(SelectionContext& ctx, hir::Inst& inst, lir::Block& lbb)
{
    auto newInst = std::make_unique<lir::Inst>(lir::OpCode::logical_branch, 1, 0);

    newInst->getDefinition(0) =
      lir::Arg{lir::Temp{ctx.controlFlowVars.find({&lbb, lbb.logicalSuccessors()[0]})->second, lir::RegClass::sgpr, 8}};
    lbb.instructions().push_back(std::move(newInst));
}

void
createVectorPhi(SelectionContext& ctx, hir::Inst& inst, lir::Block& lbb)
{
    auto newInst = std::make_unique<lir::Inst>(lir::OpCode::phi, 1, lbb.logicalPredecessors().size());
    for (unsigned i = 0; i < lbb.logicalPredecessors().size(); ++i) {
        newInst->getOperand(i) = lir::Arg{getReg(ctx, *inst.getOperand(i))};
    }
    newInst->getDefinition(0) = lir::Arg{getReg(ctx, inst)};

    lbb.instructions().push_back(std::move(newInst));
}

void
createBlockStart(SelectionContext& ctx, lir::Block& lbb, hir::Program& program)
{
    if (lbb.linearizedPredecessors().empty()) {
        createStartInstruction(ctx, *ctx.lprog, program);
        return;
    }

    auto newInst = std::make_unique<lir::Inst>(lir::OpCode::start_block, 0, lbb.logicalPredecessors().size());
    for (unsigned i = 0; i < lbb.logicalPredecessors().size(); ++i) {
        newInst->getOperand(i) = lir::Arg{
          lir::Temp{ctx.controlFlowVars.find({lbb.logicalPredecessors()[i], &lbb})->second, lir::RegClass::sgpr, 8}};
    }

    lbb.instructions().push_back(std::move(newInst));
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
        ctx.lprog->blocks().push_back(std::make_unique<lir::Block>(bb->id()));
    }
    for (int i = program.basicBlocks().size() - 1; i >= 0; --i) {
        auto& bb = *program.basicBlocks()[i];
        auto& lbb = *ctx.lprog->blocks()[i];

        if (i + 1 < program.basicBlocks().size()) {
            lir::findOrInsertBlock(lbb.linearizedSuccessors(), ctx.lprog->blocks()[i + 1].get());
            lir::findOrInsertBlock(ctx.lprog->blocks()[i + 1]->linearizedPredecessors(), &lbb);
        }

        for (auto pred : bb.predecessors())
            lbb.logicalPredecessors().push_back(ctx.lprog->blocks()[pred->id()].get());

        for (auto succ : bb.successors()) {
            lbb.logicalSuccessors().push_back(ctx.lprog->blocks()[succ->id()].get());
            ctx.controlFlowVars.insert({{&lbb, ctx.lprog->blocks()[succ->id()].get()}, ctx.lprog->allocateId()});
        }
    }

    for (int i = program.basicBlocks().size() - 1; i >= 0; --i) {
        auto& bb = *program.basicBlocks()[i];
        auto& lbb = *ctx.lprog->blocks()[i];

        bool emittedBlockStart = false;
        for (auto& insn : boost::adaptors::reverse(bb.instructions())) {
            switch (insn.opCode()) {
                case hir::OpCode::ret:
                    lbb.instructions().push_back(std::make_unique<lir::Inst>(lir::OpCode::s_endpgm, 0, 0));
                    break;
                case hir::OpCode::gcnInterpolate: {
                    auto attribute = static_cast<hir::ScalarConstant*>(insn.getOperand(3))->integerValue();
                    auto component = static_cast<hir::ScalarConstant*>(insn.getOperand(4))->integerValue();
                    auto p1 = std::make_unique<lir::Inst>(lir::OpCode::v_interp_p1_f32, 1, 2); //(attribute, component);
                    auto p2 = std::make_unique<lir::Inst>(lir::OpCode::v_interp_p2_f32, 1, 3); //(attribute, component);

                    lir::Temp tmp{lprog->allocateId(), lir::RegClass::vgpr, 4};
                    p1->getDefinition(0) = lir::Arg{tmp};
                    p1->getOperand(0) = lir::Arg{getSingleVGPR(ctx, *insn.getOperand(1))};
                    p1->getOperand(1) = lir::Arg{getSingleSGPR(ctx, *insn.getOperand(0)), lir::PhysReg{124}};
                    p1->aux().vintrp.attribute = attribute;
                    p1->aux().vintrp.channel = component;

                    p2->getDefinition(0) = lir::Arg{getReg(ctx, insn, lir::RegClass::vgpr, 4)};
                    p2->getOperand(0) = lir::Arg{tmp};
                    p2->getOperand(1) = lir::Arg{getSingleVGPR(ctx, *insn.getOperand(2))};
                    p2->getOperand(2) = lir::Arg{getSingleSGPR(ctx, *insn.getOperand(0)), lir::PhysReg{124}};
                    p2->aux().vintrp.attribute = attribute;
                    p2->aux().vintrp.channel = component;

                    lbb.instructions().emplace_back(std::move(p2));
                    lbb.instructions().emplace_back(std::move(p1));
                } break;
                case hir::OpCode::gcnExport: {
                    auto exp = std::make_unique<lir::Inst>(lir::OpCode::exp, 0, 4);

                    exp->getOperand(0) = lir::Arg{getSingleVGPR(ctx, *insn.getOperand(3))};
                    exp->getOperand(1) = lir::Arg{getSingleVGPR(ctx, *insn.getOperand(4))};
                    exp->getOperand(2) = lir::Arg{getSingleVGPR(ctx, *insn.getOperand(5))};
                    exp->getOperand(3) = lir::Arg{getSingleVGPR(ctx, *insn.getOperand(6))};
                    exp->aux().exp.enable = static_cast<hir::ScalarConstant*>(insn.getOperand(0))->integerValue();
                    exp->aux().exp.target = static_cast<hir::ScalarConstant*>(insn.getOperand(1))->integerValue();
                    exp->aux().exp.compressed = static_cast<hir::ScalarConstant*>(insn.getOperand(2))->integerValue();
                    exp->aux().exp.done = true;
                    exp->aux().exp.validMask = true;

                    lbb.instructions().emplace_back(std::move(exp));
                } break;
                case hir::OpCode::orderedLessThan:
                    createVectorCompare(ctx, lir::OpCode::v_cmp_lt_f32, insn, lbb);
                    break;
                case hir::OpCode::phi: {
                    if (!emittedBlockStart) {
                        createBlockStart(ctx, lbb, program);
                        emittedBlockStart = true;
                    }
                    if (ctx.regClasses[insn.id()] == lir::RegClass::vgpr)
                        createVectorPhi(ctx, insn, lbb);
                    else
                        std::terminate();
                } break;
                case hir::OpCode::condBranch: {
                    createLogicalCondBranch(ctx, insn, lbb);
                } break;
                case hir::OpCode::branch:
                    createLogicalBranch(ctx, insn, lbb);
                    break;
                default:
                    std::terminate();
            }
        }
        if (!emittedBlockStart) {
            createBlockStart(ctx, lbb, program);
            emittedBlockStart = true;
        }
    }

    for (auto& b : ctx.lprog->blocks())
        std::reverse(b->instructions().begin(), b->instructions().end());
    return lprog;
}
}
}
