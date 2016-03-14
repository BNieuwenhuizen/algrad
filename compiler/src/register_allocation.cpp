#include "lir.hpp"

#include <iostream>
#include <unordered_map>
#include <unordered_set>

#include <boost/range/adaptor/reversed.hpp>

namespace algrad {
namespace compiler {

using LiveSet = std::unordered_map<unsigned, std::pair<lir::RegClass, unsigned>>;

LiveSet
getLiveOut(std::vector<LiveSet>& liveIn, lir::Block& bb)
{
    LiveSet ret;
    for (int logical = 0; logical < 2; ++logical) {
        for (auto succ : (logical ? bb.logicalSuccessors() : bb.linearizedSuccessors())) {
            ret.insert(liveIn[succ->id()].begin(), liveIn[succ->id()].end());
            int index = -1;
            unsigned i = 0;
            for (auto pred : (logical ? succ->logicalPredecessors() : succ->linearizedPredecessors())) {
                if (pred == &bb)
                    index = i;
                ++i;
            }

            for (auto& insn : succ->instructions()) {
                if (insn->opCode() != lir::OpCode::phi)
                    break;
                if ((logical && insn->getDefinition(0).regClass() == lir::RegClass::vgpr) ||
                    (!logical && insn->getDefinition(0).regClass() != lir::RegClass::vgpr))
                    ret.insert({insn->getOperand(index).tempId(),
                                {insn->getOperand(index).regClass(), insn->getOperand(index).size()}});
            }
        }
    }
    return ret;
}
std::vector<LiveSet>
computeLiveIn(lir::Program& program)
{
    std::vector<LiveSet> liveIn(program.blocks().size());

    for (;;) {
        bool changed = false;
        for (auto& bb : boost::adaptors::reverse(program.blocks())) {
            auto live = getLiveOut(liveIn, *bb);
            for (auto& insn : boost::adaptors::reverse(bb->instructions())) {
                auto defCount = insn->definitionCount();
                for (unsigned i = 0; i < defCount; ++i) {
                    auto it = live.find(insn->getDefinition(i).tempId());
                    if (it != live.end())
                        live.erase(it);
                }

                if (insn->opCode() != lir::OpCode::phi) {
                    auto opCount = insn->operandCount();
                    for (unsigned i = 0; i < opCount; ++i) {
                        if (insn->getOperand(i).isTemp())
                            live.insert({insn->getOperand(i).tempId(),
                                         {insn->getOperand(i).regClass(), insn->getOperand(i).size()}});
                    }
                }
            }
            if (live != liveIn[bb->id()]) {
                liveIn[bb->id()] = live;
                changed = true;
            }
        }
        if (!changed)
            break;
    }
    return liveIn;
}

void
insertCopies(lir::Program& program)
{
    auto liveIn = computeLiveIn(program);
    for (auto& bb : program.blocks()) {
        std::vector<std::unique_ptr<lir::Inst>> instructions;
        instructions.reserve(bb->instructions().size());
        auto live = getLiveOut(liveIn, *bb);

        for (auto it = bb->instructions().end(); it != bb->instructions().begin();) {
            --it;
            auto& insn = *it;
            bool needMove = false;
            auto defCount = insn->definitionCount();
            for (std::size_t i = 0; i < defCount; ++i) {
                auto const& def = insn->getDefinition(i);
                if (def.isTemp() && def.isFixed())
                    needMove = true;
                auto it = live.find(def.tempId());
                if (it != live.end())
                    live.erase(it);
            }

            auto opCount = insn->operandCount();
            for (std::size_t i = 0; i < opCount; ++i) {
                auto arg = insn->getOperand(i);
                if (arg.isTemp()) {
                    if (arg.isFixed())
                        needMove = true;
                    live[arg.tempId()] = {arg.regClass(), arg.size()};
                }
            }

            instructions.push_back(std::move(insn));
            if (needMove && !live.empty()) {
                auto copy = std::make_unique<lir::Inst>(lir::OpCode::parallel_copy, live.size(), live.size());
                unsigned idx = 0;
                for (auto e : live) {
                    copy->getOperand(idx) = lir::Arg{lir::Temp{e.first, e.second.first, e.second.second}};
                    copy->getDefinition(idx) = lir::Arg{lir::Temp{e.first, e.second.first, e.second.second}};
                    ++idx;
                }
                instructions.push_back(std::move(copy));
            }
        }

        std::reverse(instructions.begin(), instructions.end());
        bb->instructions() = std::move(instructions);
    }
}

void
fixSSA(lir::Program& program)
{
    std::unordered_map<std::uint32_t, std::uint32_t> renames;
    for (auto& bb : program.blocks()) {
        for (auto& insn : bb->instructions()) {
            auto opCount = insn->operandCount();
            for (std::size_t i = 0; i < opCount; ++i) {
                auto& arg = insn->getOperand(i);

                if (arg.isTemp()) {
                    auto it = renames.find(arg.tempId());
                    if (it != renames.end())
                        arg.setTempId(it->second);
                }
            }

            auto defCount = insn->definitionCount();
            for (std::size_t i = 0; i < defCount; ++i) {
                auto& def = insn->getDefinition(i);
                if (def.isTemp()) {
                    auto it = renames.find(def.tempId());
                    if (it == renames.end()) {
                        renames.insert({def.tempId(), def.tempId()});
                    } else {
                        auto next = program.allocateId();
                        it->second = next;
                        def.setTempId(next);
                    }
                }
            }
        }
    }
}

void
colorRegisters(lir::Program& program)
{
    std::vector<std::unordered_set<unsigned>> ig(program.allocatedIds());
    std::vector<int> colors(program.allocatedIds(), -1);

    for (auto& bb : program.blocks()) {
        std::unordered_map<unsigned, lir::Temp> live;
        for (auto it = bb->instructions().rbegin(); it != bb->instructions().rend(); ++it) {
            auto& inst = *it;

            auto defCount = inst->definitionCount();
            for (std::size_t i = 0; i < defCount; ++i) {
                auto def = inst->getDefinition(i);
                if (def.isTemp()) {
                    auto it = live.find(def.tempId());
                    if (it != live.end())
                        live.erase(it);
                }
            }

            auto opCount = inst->operandCount();
            for (std::size_t i = 0; i < opCount; ++i) {
                auto& arg = inst->getOperand(i);
                if (arg.isTemp()) {
                    if (live.find(arg.tempId()) == live.end())
                        arg.setKill(live.find(arg.tempId()) == live.end());
                }
            }

            for (std::size_t i = 0; i < opCount; ++i) {
                auto& arg = inst->getOperand(i);
                if (arg.isTemp()) {
                    live[arg.tempId()] = arg.getTemp();
                }
            }
        }
    }
    for (auto& bb : program.blocks()) {
        std::vector<bool> colorsUsed(512);
        for (auto it = bb->instructions().begin(); it != bb->instructions().end(); ++it) {
            auto opCount = (*it)->operandCount();
            for (std::size_t i = 0; i < opCount; ++i) {
                auto& arg = (*it)->getOperand(i);
                if (arg.isTemp()) {
                    if (arg.kill())
                        colorsUsed[colors[arg.tempId()]] = false;
                    std::cout << it->get() << " " << arg.tempId() << " " << colors[arg.tempId()] << "\n";
                    arg.setFixed(lir::PhysReg{static_cast<unsigned>(colors[arg.tempId()])});
                }
            }
            auto defCount = (*it)->definitionCount();
            for (std::size_t i = 0; i < defCount; ++i) {
                auto& def = (*it)->getDefinition(i);
                if (def.isTemp()) {
                    if (colors[def.tempId()] < 0) {
                        std::vector<bool> forbidden = colorsUsed;
                        int c = -1;
                        if (def.isFixed()) {
                            c = def.physReg().reg;
                        }
                        if (it + 1 != bb->instructions().end()) {
                            auto opCount2 = it[1]->operandCount();
                            for (unsigned j = 0; j < opCount2; ++j) {
                                auto const& arg2 = it[1]->getOperand(j);
                                if (arg2.isFixed()) {
                                    if (def.tempId() != arg2.tempId())
                                        forbidden[arg2.physReg().reg] = true;
                                    else
                                        c = arg2.physReg().reg;
                                }
                            }
                        }
                        if (c == -1 && (*it)->opCode() == lir::OpCode::parallel_copy) {
                            auto prevArg = (*it)->getOperand(i);
                            if (!forbidden[prevArg.physReg().reg]) {
                                c = prevArg.physReg().reg;
                            }
                        }

                        if (c == -1) {
                            c = def.regClass() == lir::RegClass::vgpr ? 256 : 0;
                            while (forbidden[c])
                                ++c;
                        }
                        colorsUsed[c] = true;
                        colors[def.tempId()] = c;
                    }
                    def.setFixed(lir::PhysReg{static_cast<unsigned>(colors[def.tempId()])});
                }
            }
        }
    }
}

void
allocateRegisters(lir::Program& program)
{
    insertCopies(program);
    fixSSA(program);
    colorRegisters(program);
}
}
}
