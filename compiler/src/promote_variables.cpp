#include "hir.hpp"

#include <iostream>

namespace algrad {
namespace compiler {

using namespace hir;

template <typename F>
void
visitInstructions(BasicBlock& bb, F&& callback)
{
    auto& insns = bb.instructions();
    unsigned j = 0;
    std::vector<std::unique_ptr<Inst>> toDestroy;

    for (unsigned i = 0; i < insns.size(); ++i) {
        auto& insn = insns[i];
        for (auto& p : insn->operands()) {
            if (p->opCode() == OpCode::identity)
                p = static_cast<Inst*>(p)->operands()[0];
        }
        callback(*insn);
        if (insn->opCode() != OpCode::identity) {
            if (j != i)
                insns[j] = std::move(insns[i]);
            ++j;
        } else
            toDestroy.push_back(std::move(insns[i]));
    }
    if (j != insns.size())
        insns.resize(j);
}

bool
splitVariables(Program& program)
{
    std::vector<bool> cannotBeSplit(program.defIdCount());
    std::vector<int> newVarOffsets(program.defIdCount(), -1);
    std::vector<Inst*> newVars;

    for (auto& bb : program.basicBlocks()) {
        visitInstructions(*bb, [&cannotBeSplit](Inst& insn) {
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
    std::vector<std::unique_ptr<Inst>> oldVars;
    oldVars.reserve(program.variables().size());
    std::swap(oldVars, program.variables());

    bool changed = false;
    for (auto& v : oldVars) {
        if (!cannotBeSplit[v->id()]) {
            changed = true;
            std::cout << "split " << v->id() << "\n";

            newVarOffsets[v->id()] = newVars.size();
            auto type = static_cast<PointerTypeInfo const*>(v->type())->pointeeType();
            auto count = compositeCount(type);
            for (std::size_t i = 0; i < count; ++i) {
                auto ptrType = program.types().pointerType(type, StorageKind::invocation);
                auto newVar = program.createDef<Inst>(OpCode::variable, ptrType, 0);
                newVars.push_back(newVar.get());
                program.variables().push_back(std::move(newVar));
            }

        } else
            program.variables().push_back(std::move(v));
    }

    for (auto& bb : program.basicBlocks()) {
        visitInstructions(*bb, [&cannotBeSplit, &newVars, &newVarOffsets](Inst& insn) {
            if (insn.opCode() == OpCode::accessChain && newVarOffsets[insn.operands()[0]->id()] >= 0) {
                auto idx = static_cast<ScalarConstant*>(insn.operands()[1])->integerValue();
                auto replacement = newVars[newVarOffsets[insn.operands()[0]->id()] + idx];

                if (insn.operands().size() == 2) {
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
promoteVariables(Program& program)
{
    splitVariables(program);
    std::vector<bool> canPromote(program.defIdCount());
    std::vector<Def*> promotedValues(program.defIdCount());
    for (auto& var : program.variables()) {
        canPromote[var->id()] = true;
    }
    for (auto& bb : program.basicBlocks()) {
        visitInstructions(*bb, [&canPromote](Inst& insn) {
            if (insn.opCode() == OpCode::store)
                canPromote[insn.operands()[1]->id()] = false;
            else if (insn.opCode() != OpCode::load) {
                for (auto op : insn.operands())
                    canPromote[op->id()] = false;
            }
        });
    }
    for (auto& bb : program.basicBlocks()) {
        visitInstructions(*bb, [&canPromote, &promotedValues](Inst& insn) {
            if (insn.opCode() == OpCode::store) {
                if (canPromote[insn.operands()[0]->id()]) {
                    promotedValues[insn.operands()[0]->id()] = insn.operands()[1];
                    insn.identify(nullptr);
                }
            } else if (insn.opCode() == OpCode::load) {
                if (canPromote[insn.operands()[0]->id()]) {
                    insn.identify(promotedValues[insn.operands()[0]->id()]);
                }
            }
        });
    }
    program.variables().erase(std::remove_if(program.variables().begin(), program.variables().end(),
                                             [&canPromote](auto& v) { return canPromote[v->id()]; }),
                              program.variables().end());
}
}
}
