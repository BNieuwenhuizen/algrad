#include "hir.hpp"
#include "hir_inlines.hpp"

#include <iostream>
#include <unordered_map>

#include <boost/range/adaptor/reversed.hpp>

namespace algrad {
namespace compiler {

using namespace hir;

template <typename F>
void
visitInstructions(BasicBlock& bb, F&& callback)
{
    for (auto& inst : bb.instructions())
        callback(inst);
}

bool
splitVariables(Program& program)
{
    std::vector<bool> canBeSplit(program.defIdCount());
    std::vector<int> newVarOffsets(program.defIdCount(), -1);
    std::vector<Inst*> newVars;

    for (auto& insn : program.variables())
        canBeSplit[insn.id()] = true;

    for (auto& bb : program.basicBlocks()) {
        visitInstructions(*bb, [&canBeSplit](Inst& insn) {
            if (insn.opCode() == OpCode::accessChain) {
                if (insn.operandCount() < 2 || insn.getOperand(1)->opCode() != OpCode::constant) {
                    canBeSplit[insn.getOperand(0)->id()] = false;
                }
            } else {
                std::size_t operandCount = insn.operandCount();
                for (std::size_t i = 0; i < operandCount; ++i) {
                    canBeSplit[insn.getOperand(i)->id()] = false;
                }
            }
        });
    }

    std::vector<std::unique_ptr<Inst>> toBeDeleted;
    for (auto it = program.variables().begin(); it != program.variables().end();) {
        auto& v = *it++;
        if (canBeSplit[v.id()]) {
            newVarOffsets[v.id()] = newVars.size();
            auto type = static_cast<PointerTypeInfo const*>(v.type())->pointeeType();
            auto count = compositeCount(type);
            for (std::size_t i = 0; i < count; ++i) {
                auto ptrType = program.types().pointerType(compositeType(type, i), StorageKind::invocation);
                auto newVar = program.createDef<Inst>(OpCode::variable, ptrType, 0);
                newVars.push_back(newVar.get());
                program.insertVariable(std::move(newVar));
            }
            toBeDeleted.push_back(program.eraseVariable(v));
        }
    }

    for (auto& bb : program.basicBlocks()) {
        for (auto it = bb->instructions().begin(); it != bb->instructions().end();) {
            auto& insn = *it++;
            if (insn.opCode() == OpCode::accessChain) {
                auto baseId = insn.getOperand(0)->id();
                if (newVarOffsets[baseId] >= 0) {
                    auto idx = static_cast<ScalarConstant*>(insn.getOperand(1))->integerValue();
                    auto replacement = newVars[newVarOffsets[baseId] + idx];

                    if (insn.operandCount() == 2) {
                        replace(insn, *replacement);
                        bb->erase(insn);
                    } else {
                        insn.setOperand(0, newVars[newVarOffsets[baseId] + idx]);
                        insn.eraseOperand(1);
                    }
                }
            }
        }
    }

    return !toBeDeleted.empty();
}

void
promoteVariables(Program& program)
{
    splitVariables(program);

    std::vector<int> canPromote(program.defIdCount(), -1);
    int idx = 0;
    for (auto& var : program.variables()) {
        canPromote[var.id()] = idx++;
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
            auto it = program.variables().begin();
            for (std::size_t i = 0; i < promotedValues.size(); ++i, ++it) {
                phis.push_back(program.createDef<Inst>(OpCode::phi,
                                                       static_cast<PointerTypeInfo const*>(it->type())->pointeeType(),
                                                       bb->predecessors().size()));

                promotedValues[i] = phis.back().get();
            }
        }
        for (auto it = bb->instructions().begin(); it != bb->instructions().end();) {
            auto& insn = *it++;
            if (insn.opCode() == OpCode::store) {
                auto baseId = insn.getOperand(0)->id();
                if (canPromote[baseId] >= 0) {
                    promotedValues[canPromote[baseId]] = insn.getOperand(1);
                    bb->erase(insn);
                }
            } else if (insn.opCode() == OpCode::load) {
                auto baseId = insn.getOperand(0)->id();
                if (canPromote[baseId] >= 0) {
                    replace(insn, *promotedValues[canPromote[baseId]]);
                    bb->erase(insn);
                }
            }
        }

        for (auto& insn : boost::adaptors::reverse(phis)) {
            bb->insertFront(std::move(insn));
        }

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

            auto it = succ->instructions().begin();
            for (std::size_t i = 0; i < defs.size(); ++i, ++it) {
                it->setOperand(idx, defs[i]);
            }
        }
    }

    for (auto it = program.variables().begin(); it != program.variables().end();) {
        auto& v = *it++;
        if (v.uses().empty())
            program.eraseVariable(v);
    }
}
}
}
