#include "ir.hpp"

#include <iostream>

namespace algrad {
namespace compiler {

namespace {
void
splitLoad(Program& program, Function& function, std::vector<std::unique_ptr<Instruction>>& replacements, BasicBlock& bb,
          std::unique_ptr<Instruction>&& insn)
{
    if (!isComposite(insn->type())) {
        bb.insertBack(std::move(insn));
        return;
    }
    auto count = compositeCount(insn->type());
    auto newInsn = program.createDef<Instruction>(OpCode::compositeConstruct, insn->type(), count);

    auto storage = static_cast<PointerTypeInfo const*>(insn->operands()[0]->type())->storage();
    for (std::size_t i = 0; i < count; ++i) {
        auto type = compositeType(insn->type(), i);
        auto ptrType = program.types().pointerType(type, storage);
        auto& addr = bb.insertBack(program.createDef<Instruction>(OpCode::accessChain, ptrType, 2));
        addr.operands()[0] = insn->operands()[0];
        addr.operands()[1] = program.getScalarConstant(&int32Type, i);

        auto& load = bb.insertBack(program.createDef<Instruction>(OpCode::load, type, 1));
        load.operands()[0] = &addr;
        newInsn->operands()[i] = &load;
    }

    insn->identify(newInsn.get());
    bb.insertBack(std::move(newInsn));
    replacements.push_back(std::move(insn));
}

void
splitStore(Program& program, Function& function, std::vector<std::unique_ptr<Instruction>>& replacements,
           BasicBlock& bb, std::unique_ptr<Instruction>&& insn)
{
    if (!isComposite(insn->operands()[1]->type())) {
        bb.insertBack(std::move(insn));
        return;
    }
    auto count = compositeCount(insn->operands()[1]->type());

    auto storage = static_cast<PointerTypeInfo const*>(insn->operands()[0]->type())->storage();
    for (std::size_t i = 0; i < count; ++i) {
        auto type = compositeType(insn->operands()[0]->type(), i);
        auto ptrType = program.types().pointerType(type, storage);

        auto& addr = bb.insertBack(program.createDef<Instruction>(OpCode::accessChain, ptrType, 2));
        addr.operands()[0] = insn->operands()[0];
        addr.operands()[1] = program.getScalarConstant(&int32Type, i);

        Def* elem;

        if (insn->operands()[1]->opCode() == OpCode::compositeConstruct) {
            elem = static_cast<Instruction*>(insn->operands()[1])->operands()[i];
        } else {
            Instruction& elemInsn = bb.insertBack(program.createDef<Instruction>(OpCode::compositeExtract, type, 2));
            elemInsn.operands()[0] = insn->operands()[1];
            elemInsn.operands()[1] = program.getScalarConstant(&int32Type, i);
            elem = &elemInsn;
        }

        auto& store = bb.insertBack(program.createDef<Instruction>(OpCode::store, &voidType, 2));
        store.operands()[0] = &addr;
        store.operands()[1] = elem;
    }
}

void
splitComposites(Program& program, Function& function, std::vector<std::unique_ptr<Instruction>>& replacements)
{
    for (auto& bb : function.basicBlocks()) {
        std::vector<std::unique_ptr<Instruction>> oldInstructions;
        oldInstructions.reserve(bb->instructions().size());
        std::swap(oldInstructions, bb->instructions());

        for (auto& insn : oldInstructions) {
            for (auto& op : insn->operands()) {
                if (op->opCode() == OpCode::identity)
                    op = static_cast<Instruction*>(op)->operands()[0];
            }
            switch (insn->opCode()) {
                case OpCode::load:
                    splitLoad(program, function, replacements, *bb, std::move(insn));
                    break;
                case OpCode::store:
                    splitStore(program, function, replacements, *bb, std::move(insn));
                    break;
                default:
                    bb->insertBack(std::move(insn));
                    break;
            }
        }
    }
}
}

void
splitComposites(Program& program)
{
    std::vector<std::unique_ptr<Instruction>> replacements;
    for (auto& func : program.functions())
        splitComposites(program, *func, replacements);
}
}
}
