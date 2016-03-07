#include "hir.hpp"
#include "hir_inlines.hpp"

#include <iostream>

namespace algrad {
namespace compiler {

using namespace hir;
namespace {

Def&
extractComponent(hir::Program& program, hir::BasicBlock& bb, Inst& inst, Def& def, unsigned index)
{
    if (def.opCode() == OpCode::compositeConstruct) {
        return *static_cast<Inst&>(def).getOperand(index);
    } else {
        Inst& elemInsn =
          bb.insertBefore(inst, program.createDef<Inst>(OpCode::compositeExtract, compositeType(def.type(), index), 2));
        elemInsn.setOperand(0, &def);
        elemInsn.setOperand(1, program.getScalarConstant(&int32Type, static_cast<std::uint64_t>(index)));
        return elemInsn;
    }
}

void
splitLoad(Program& program, BasicBlock& bb, Inst& insn)
{
    if (!isComposite(insn.type())) {
        return;
    }
    auto count = compositeCount(insn.type());
    auto newInsn = program.createDef<Inst>(OpCode::compositeConstruct, insn.type(), count);

    auto storage = static_cast<PointerTypeInfo const*>(insn.getOperand(0)->type())->storage();
    for (std::size_t i = 0; i < count; ++i) {
        auto type = compositeType(insn.type(), i);
        auto ptrType = program.types().pointerType(type, storage);
        auto& addr = bb.insertBefore(insn, program.createDef<Inst>(OpCode::accessChain, ptrType, 2));
        addr.setOperand(0, insn.getOperand(0));
        addr.setOperand(1, program.getScalarConstant(&int32Type, i));

        auto& load = bb.insertBefore(insn, program.createDef<Inst>(OpCode::load, type, 1));
        load.setOperand(0, &addr);
        newInsn->setOperand(i, &load);
    }

    replace(insn, *newInsn);
    bb.insertBefore(insn, std::move(newInsn));
    bb.erase(insn);
}

void
splitStore(Program& program, BasicBlock& bb, Inst& insn)
{
    if (!isComposite(insn.getOperand(1)->type())) {
        return;
    }
    auto count = compositeCount(insn.getOperand(1)->type());

    auto storage = static_cast<PointerTypeInfo const*>(insn.getOperand(0)->type())->storage();
    for (std::size_t i = 0; i < count; ++i) {
        auto type = compositeType(insn.getOperand(0)->type(), i);
        auto ptrType = program.types().pointerType(type, storage);

        auto& addr = bb.insertBefore(insn, program.createDef<Inst>(OpCode::accessChain, ptrType, 2));
        addr.setOperand(0, insn.getOperand(0));
        addr.setOperand(1, program.getScalarConstant(&int32Type, i));

        auto& elem = extractComponent(program, bb, insn, *insn.getOperand(1), i);
        auto& store = bb.insertBefore(insn, program.createDef<Inst>(OpCode::store, &voidType, 2));
        store.setOperand(0, &addr);
        store.setOperand(1, &elem);
    }

    bb.erase(insn);
}

void
splitVectorShuffle(Program& program, BasicBlock& bb, Inst& insn)
{
    if (!isComposite(insn.type())) {
        std::terminate();
    }
    auto count = compositeCount(insn.type());
    auto& newInsn = bb.insertBefore(insn, program.createDef<Inst>(OpCode::compositeConstruct, insn.type(), count));

    auto op1Count = compositeCount(insn.getOperand(0)->type());

    for (std::size_t i = 0; i < count; ++i) {
        auto index = static_cast<ScalarConstant*>(insn.getOperand(2 + i))->integerValue();
        Def* op = insn.getOperand(0);
        if (index >= op1Count) {
            op = insn.getOperand(1);
            index -= op1Count;
        }

        newInsn.setOperand(i, &extractComponent(program, bb, insn, *op, index));
    }

    replace(insn, newInsn);
    bb.erase(insn);
}
}

void
splitComposites(Program& program)
{
    for (auto& bb : program.basicBlocks()) {
        for (auto it = bb->instructions().begin(); it != bb->instructions().end();) {
            auto& inst = *it++;
            switch (inst.opCode()) {
                case OpCode::load:
                    splitLoad(program, *bb, inst);
                    break;
                case OpCode::store:
                    splitStore(program, *bb, inst);
                    break;
                case OpCode::vectorShuffle:
                    splitVectorShuffle(program, *bb, inst);
                    break;
                default:
                    break;
            }
        }
    }
}
}
}
