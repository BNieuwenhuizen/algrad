#include "hir.hpp"
#include "hir_inlines.hpp"

#include <iostream>

namespace algrad {
namespace compiler {

using namespace hir;
namespace {
void
splitLoad(Program& program, std::vector<std::unique_ptr<Inst>>& replacements, BasicBlock& bb,
          std::unique_ptr<Inst>&& insn)
{
    if (!isComposite(insn->type())) {
        bb.insertBack(std::move(insn));
        return;
    }
    auto count = compositeCount(insn->type());
    auto newInsn = program.createDef<Inst>(OpCode::compositeConstruct, insn->type(), count);

    auto storage = static_cast<PointerTypeInfo const*>(insn->getOperand(0)->type())->storage();
    for (std::size_t i = 0; i < count; ++i) {
        auto type = compositeType(insn->type(), i);
        auto ptrType = program.types().pointerType(type, storage);
        auto& addr = bb.insertBack(program.createDef<Inst>(OpCode::accessChain, ptrType, 2));
        addr.setOperand(0, insn->getOperand(0));
        addr.setOperand(1, program.getScalarConstant(&int32Type, i));

        auto& load = bb.insertBack(program.createDef<Inst>(OpCode::load, type, 1));
        load.setOperand(0, &addr);
        newInsn->setOperand(i, &load);
    }

    insn->identify(newInsn.get());
    bb.insertBack(std::move(newInsn));
    replacements.push_back(std::move(insn));
}

void
splitStore(Program& program, std::vector<std::unique_ptr<Inst>>& replacements, BasicBlock& bb,
           std::unique_ptr<Inst>&& insn)
{
    if (!isComposite(insn->getOperand(1)->type())) {
        bb.insertBack(std::move(insn));
        return;
    }
    auto count = compositeCount(insn->getOperand(1)->type());

    auto storage = static_cast<PointerTypeInfo const*>(insn->getOperand(0)->type())->storage();
    for (std::size_t i = 0; i < count; ++i) {
        auto type = compositeType(insn->getOperand(0)->type(), i);
        auto ptrType = program.types().pointerType(type, storage);

        auto& addr = bb.insertBack(program.createDef<Inst>(OpCode::accessChain, ptrType, 2));
        addr.setOperand(0, insn->getOperand(0));
        addr.setOperand(1, program.getScalarConstant(&int32Type, i));

        Def* elem;


        if (insn->getOperand(1)->opCode() == OpCode::compositeConstruct) {
            elem = static_cast<Inst*>(insn->getOperand(1))->getOperand(i);
        } else {
            Inst& elemInsn = bb.insertBack(program.createDef<Inst>(OpCode::compositeExtract, type, 2));
            elemInsn.setOperand(0, insn->getOperand(1));
            elemInsn.setOperand(1, program.getScalarConstant(&int32Type, i));
            elem = &elemInsn;
        }

        auto& store = bb.insertBack(program.createDef<Inst>(OpCode::store, &voidType, 2));
        store.setOperand(0, &addr);
        store.setOperand(1, elem);
    }
}
}
void
splitComposites(Program& program)
{
    std::vector<std::unique_ptr<Inst>> replacements;
    for (auto& bb : program.basicBlocks()) {
        std::vector<std::unique_ptr<Inst>> oldInstructions;
        oldInstructions.reserve(bb->instructions().size());
        std::swap(oldInstructions, bb->instructions());

        for (auto& insn : oldInstructions) {
            auto operandCount = insn->operandCount();
            for (std::size_t i = 0; i < operandCount; ++i) {
                auto op = insn->getOperand(i);
                if (op->opCode() == OpCode::identity) {
                    insn->setOperand(i, static_cast<Inst*>(op)->getOperand(0));
		}
            }
            switch (insn->opCode()) {
                case OpCode::load:
                    splitLoad(program, replacements, *bb, std::move(insn));
                    break;
                case OpCode::store:
                    splitStore(program, replacements, *bb, std::move(insn));
                    break;
                default:
                    bb->insertBack(std::move(insn));
                    break;
            }
        }
    }
}
}
}
