#include "ir.hpp"

#include <iostream>

namespace algrad {
namespace compiler {

template <typename F>
void
visitInstructions(BasicBlock& bb, F&& callback)
{
    auto& insns = bb.instructions();
    unsigned j = 0;
    std::vector<std::unique_ptr<Instruction>> toDestroy;

    for (unsigned i = 0; i < insns.size(); ++i) {
        auto& insn = insns[i];
        for (auto& p : insn->operands()) {
            if (p->opCode() == OpCode::identity)
                p = static_cast<Instruction*>(p)->operands()[0];
        }
        callback(*insn);
        if(insn->opCode() != OpCode::identity) {
            if(j != i)
                insns[j] = std::move(insns[i]);
            ++j;
        } else
            toDestroy.push_back(std::move(insns[i]));
    }
    if(j != insns.size())
        insns.resize(j);
}

bool
splitVariables(Program& program, Function& function)
{
    std::vector<bool> cannotBeSplit(program.defIdCount());
    std::vector<int> newVarOffsets(program.defIdCount(), -1);
    std::vector<Instruction*> newVars;

    for (auto& bb : function.basicBlocks()) {
        visitInstructions(*bb, [&cannotBeSplit](Instruction& insn) {
            if (insn.opCode() == OpCode::accessChain) {
                if (insn.operands().size() < 2 || insn.operands()[1]->opCode() != OpCode::constant)
                    cannotBeSplit[insn.operands()[0]->id()] = true;
            } else {

                for (auto op : insn.operands()) {
                    if (op->id() == 1) {
                        std::cout << " " << insn.id() << "\n";
                    }
                    cannotBeSplit[op->id()] = true;
                }
            }
        });
    }
    std::vector<std::unique_ptr<Instruction>> oldVars;
    oldVars.reserve(function.variables().size());
    std::swap(oldVars, function.variables());

    bool changed = false;
    for (auto& v : oldVars) {
        if (!cannotBeSplit[v->id()]) {
            changed = true;
            std::cout << "split " << v->id() << "\n";

            newVarOffsets[v->id()] = newVars.size();
            auto type = static_cast<PointerTypeInfo const*>(v->type())->pointeeType();
            auto count = compositeCount(type);
            for(std::size_t i = 0; i < count; ++i) {
                auto ptrType = program.types().pointerType(type, StorageKind::function);
                auto newVar = program.createDef<Instruction>(OpCode::variable, ptrType, 0);
                newVars.push_back(newVar.get());
                function.variables().push_back(std::move(newVar));
            }

        } else
            function.variables().push_back(std::move(v));
    }

    for (auto& bb : function.basicBlocks()) {
        visitInstructions(*bb, [&cannotBeSplit, &newVars, &newVarOffsets](Instruction& insn) {
            if (insn.opCode() == OpCode::accessChain && newVarOffsets[insn.operands()[0]->id()] >= 0) {
                auto idx = static_cast<ScalarConstant*>(insn.operands()[1])->integerValue();
                auto replacement = newVars[newVarOffsets[insn.operands()[0]->id()] + idx];

                if(insn.operands().size() == 2) {
                    insn.identify(replacement);
                } else {
                    insn.operands()[0] = newVars[newVarOffsets[insn.operands()[0]->id()] + idx];
                    insn.eraseOperand(1);
                }
            }
        });
    }

    return changed;
}

void
splitVariables(Program& program)
{
    for (auto& func : program.functions())
        splitVariables(program, *func);
}


void
promoteVariables(Program& program)
{
    splitVariables(program);
    std::vector<bool> canPromote(program.defIdCount());
    std::vector<Def*> promotedValues(program.defIdCount());
    for(auto& func : program.functions()) {
        for(auto& var : func->variables()) {
            canPromote[var->id()] = true;
        }
        for(auto& bb : func->basicBlocks()) {
            visitInstructions(*bb, [&canPromote](Instruction& insn) {
                if(insn.opCode() == OpCode::store)
                    canPromote[insn.operands()[1]->id()] = false;
                else if(insn.opCode() != OpCode::load) {
                    for(auto op : insn.operands())
                        canPromote[op->id()] = false;
                }
            });
        }
        for(auto& bb : func->basicBlocks()) {
            visitInstructions(*bb, [&canPromote, &promotedValues](Instruction& insn) {
                if(insn.opCode() == OpCode::store) {
                    if(canPromote[insn.operands()[0]->id()]) {
                        promotedValues[insn.operands()[0]->id()] = insn.operands()[1];
                        insn.identify(nullptr);
                    }
                } else if(insn.opCode() == OpCode::load) {
                    if(canPromote[insn.operands()[0]->id()]) {
                        insn.identify(promotedValues[insn.operands()[0]->id()]);
                    }
                }
            });
        }
        func->variables().erase(std::remove_if(func->variables().begin(), func->variables().end(), [&canPromote](auto& v) { return canPromote[v->id()]; }), func->variables().end());
    }
}
}
}
