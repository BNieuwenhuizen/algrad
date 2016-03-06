#include "hir.hpp"
#include "hir_inlines.hpp"

#include <iostream>
#include <unordered_map>

namespace algrad {
namespace compiler {

using namespace hir;

template <typename F>
void
visitInstructions(BasicBlock& bb, F&& callback)
{
    for (auto& inst : bb.instructions())
        callback(*inst);
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
                if (insn.operandCount() < 2 || insn.getOperand(1)->opCode() != OpCode::constant) {
                    cannotBeSplit[insn.getOperand(0)->id()] = true;
                }
            } else {
                std::size_t operandCount = insn.operandCount();
                for (std::size_t i = 0; i < operandCount; ++i) {
                    if (insn.getOperand(i)->id() == 0) {
                        std::cout << insn.id() << " " << static_cast<unsigned>(insn.opCode()) << "\n";
                    }
                    cannotBeSplit[insn.getOperand(i)->id()] = true;
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

            newVarOffsets[v->id()] = newVars.size();
            auto type = static_cast<PointerTypeInfo const*>(v->type())->pointeeType();
            auto count = compositeCount(type);
            for (std::size_t i = 0; i < count; ++i) {
                auto ptrType = program.types().pointerType(type, StorageKind::invocation);
                auto newVar = program.createDef<Inst>(OpCode::variable, ptrType, 0);
                newVars.push_back(newVar.get());
                program.variables().push_back(std::move(newVar));
            }
        } else {
            program.variables().push_back(std::move(v));
        }
    }

    for (auto& bb : program.basicBlocks()) {
        std::size_t index = 0;
        for (std::size_t i = 0; i < bb->instructions().size(); ++i) {
            auto& insn = *bb->instructions()[i];
            bool toDelete = false;
            if (insn.opCode() == OpCode::accessChain) {
                auto baseId = insn.getOperand(0)->id();
                if (newVarOffsets[baseId] >= 0) {
                    auto idx = static_cast<ScalarConstant*>(insn.getOperand(1))->integerValue();
                    auto replacement = newVars[newVarOffsets[baseId] + idx];

                    if (insn.operandCount() == 2) {
                        replace(insn, *replacement);
                        toDelete = true;
                    } else {
                        insn.setOperand(0, newVars[newVarOffsets[baseId] + idx]);
                        insn.eraseOperand(1);
                    }
                }
            }
            if (!toDelete)
                bb->instructions()[index++] = std::move(bb->instructions()[i]);
        }
        if (index != bb->instructions().size())
            bb->instructions().resize(index);
    }

    return changed;
}

void
promoteVariables(Program& program)
{
    splitVariables(program);
    std::vector<int> canPromote(program.defIdCount(), -1);
    int idx = 0;
    for (auto& var : program.variables()) {
        canPromote[var->id()] = idx++;
    }

    for (auto& bb : program.basicBlocks()) {
        visitInstructions(*bb, [&canPromote](Inst& insn) {
            if (insn.opCode() == OpCode::store)
                canPromote[insn.getOperand(1)->id()] = -1;
            else if (insn.opCode() != OpCode::load) {
                auto operandCount = insn.operandCount();
                for (std::size_t j = 0; j < operandCount; ++j) {
                    canPromote[insn.getOperand(j)->id()] = -1;
                }
            }
        });
    }

    std::unordered_map<hir::BasicBlock*, std::vector<hir::Def*>> defsOut;
    for (auto& bb : program.basicBlocks()) {
        std::vector<hir::Def*> promotedValues(idx);

        if (!bb->predecessors().empty())
            promotedValues = defsOut[bb->predecessors()[0]];
        std::vector<std::unique_ptr<hir::Inst>> phis;
        if (bb->predecessors().size() > 1) {
            for (std::size_t i = 0; i < promotedValues.size(); ++i) {
                phis.push_back(program.createDef<Inst>(
                  OpCode::phi, static_cast<PointerTypeInfo const*>(program.variables()[i]->type())->pointeeType(),
                  bb->predecessors().size()));

                promotedValues[i] = phis.back().get();
            }
        }
        std::size_t index = 0;
        for (std::size_t i = 0; i < bb->instructions().size(); ++i) {
            auto& insn = *bb->instructions()[i];
            bool toBeDeleted = false;
            if (insn.opCode() == OpCode::store) {
                auto baseId = insn.getOperand(0)->id();
                if (canPromote[baseId] >= 0) {
                    promotedValues[canPromote[baseId]] = insn.getOperand(1);
                    toBeDeleted = true;
                }
            } else if (insn.opCode() == OpCode::load) {
                auto baseId = insn.getOperand(0)->id();
                if (canPromote[baseId] >= 0) {
                    replace(insn, *promotedValues[canPromote[baseId]]);
                    toBeDeleted = true;
                }
            }
            if (!toBeDeleted)
                bb->instructions()[index++] = std::move(bb->instructions()[i]);
        }

        if (index != bb->instructions().size())
            bb->instructions().resize(index);

        if (!phis.empty())
            bb->instructions().insert(bb->instructions().begin(), make_move_iterator(phis.begin()),
                                      make_move_iterator(phis.end()));

        defsOut[bb.get()] = promotedValues;
    }
    for (auto& bb : program.basicBlocks()) {
        auto& defs = defsOut[bb.get()];
        for (auto succ : bb->successors()) {
            if (succ->predecessors().size() <= 1)
                continue;
            unsigned idx = 0;
            while (succ->predecessors()[idx] != bb.get())
                ++idx;

            for (std::size_t i = 0; i < defs.size(); ++i) {
                succ->instructions()[i]->setOperand(idx, defs[i]);
            }
        }
    }

    program.variables().erase(std::remove_if(program.variables().begin(), program.variables().end(),
                                             [&canPromote](auto& v) { return canPromote[v->id()]; }),
                              program.variables().end());
}
}
}
