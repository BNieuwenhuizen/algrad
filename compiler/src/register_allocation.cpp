#include "lir.hpp"

#include <iostream>
#include <unordered_map>
#include <unordered_set>

#include <boost/range/adaptor/reversed.hpp>

namespace algrad {
namespace compiler {

using LiveSet = std::unordered_set<unsigned>;

LiveSet
getLiveOut(std::vector<LiveSet>& liveIn, lir::Program const& program, lir::Block& bb)
{
    LiveSet ret;
    for (int logical = 0; logical < 2; ++logical) {
        for (auto succ : (logical ? bb.logicalSuccessors() : bb.linearizedSuccessors())) {
            for (auto e : liveIn[succ->id()])
                if ((logical && program.temp_info(e).reg_class == lir::RegClass::vgpr) ||
                    (!logical && program.temp_info(e).reg_class != lir::RegClass::vgpr))
                    ret.insert(e);
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
                auto reg_class = program.temp_info(insn->getDefinition(0).temp()).reg_class;
                if ((logical && reg_class == lir::RegClass::vgpr) || (!logical && reg_class != lir::RegClass::vgpr))
                    ret.insert(insn->getOperand(index).temp());
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
            auto live = getLiveOut(liveIn, program, *bb);
            for (auto& insn : boost::adaptors::reverse(bb->instructions())) {
                auto defCount = insn->definitionCount();
                for (unsigned i = 0; i < defCount; ++i) {
                    auto it = live.find(insn->getDefinition(i).temp());
                    if (it != live.end())
                        live.erase(it);
                }

                if (insn->opCode() != lir::OpCode::phi) {
                    auto opCount = insn->operandCount();
                    for (unsigned i = 0; i < opCount; ++i) {
                        if (insn->getOperand(i).is_temp()) {
                            live.insert(insn->getOperand(i).temp());
                        }
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
        auto live = getLiveOut(liveIn, program, *bb);

        for (auto it = bb->instructions().end(); it != bb->instructions().begin();) {
            --it;
            auto& insn = *it;
            bool needMove = false;
            auto defCount = insn->definitionCount();
            for (std::size_t i = 0; i < defCount; ++i) {
                auto const& def = insn->getDefinition(i);
                if (def.is_temp() && def.isFixed())
                    needMove = true;
                auto it = live.find(def.temp());
                if (it != live.end())
                    live.erase(it);
            }

            auto opCount = insn->operandCount();
            for (std::size_t i = 0; i < opCount; ++i) {
                auto arg = insn->getOperand(i);
                if (arg.is_temp()) {
                    if (arg.isFixed())
                        needMove = true;
                    live.insert(arg.temp());
                }
            }

            instructions.push_back(std::move(insn));
            if (needMove && !live.empty()) {
                auto copy = std::make_unique<lir::Inst>(lir::OpCode::parallel_copy, live.size(), live.size());
                unsigned idx = 0;
                for (auto e : live) {
                    copy->getOperand(idx) = lir::Arg{e};
                    copy->getDefinition(idx) = lir::Arg{e};
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

                if (arg.is_temp()) {
                    auto it = renames.find(arg.temp());
                    if (it != renames.end())
                        arg.set_temp(it->second);
                }
            }

            auto defCount = insn->definitionCount();
            for (std::size_t i = 0; i < defCount; ++i) {
                auto& def = insn->getDefinition(i);
                if (def.is_temp()) {
                    auto it = renames.find(def.temp());
                    if (it == renames.end()) {
                        renames.insert({def.temp(), def.temp()});
                    } else {
                        auto next = program.allocate_temp(program.temp_info(def.temp()).reg_class,
                                                          program.temp_info(def.temp()).size);
                        it->second = next;
                        def.set_temp(next);
                    }
                }
            }
        }
    }
}

void
colorRegisters(lir::Program& program)
{
    std::vector<std::unordered_set<unsigned>> ig(program.allocated_temp_count());
    std::vector<int> colors(program.allocated_temp_count(), -1);

    for (auto& bb : program.blocks()) {
        std::unordered_set<unsigned> live;
        for (auto it = bb->instructions().rbegin(); it != bb->instructions().rend(); ++it) {
            auto& inst = *it;

            auto defCount = inst->definitionCount();
            for (std::size_t i = 0; i < defCount; ++i) {
                auto def = inst->getDefinition(i);
                if (def.is_temp()) {
                    auto it = live.find(def.temp());
                    if (it != live.end())
                        live.erase(it);
                }
            }

            auto opCount = inst->operandCount();
            for (std::size_t i = 0; i < opCount; ++i) {
                auto& arg = inst->getOperand(i);
                if (arg.is_temp()) {
                    if (live.find(arg.temp()) == live.end())
                        arg.setKill(live.find(arg.temp()) == live.end());
                }
            }

            for (std::size_t i = 0; i < opCount; ++i) {
                auto& arg = inst->getOperand(i);
                if (arg.is_temp()) {
                    live.insert(arg.temp());
                }
            }
        }
    }
    for (auto& bb : program.blocks()) {
        std::vector<bool> colorsUsed(2048);
        for (auto it = bb->instructions().begin(); it != bb->instructions().end(); ++it) {
            auto opCount = (*it)->operandCount();
            for (std::size_t i = 0; i < opCount; ++i) {
                auto& arg = (*it)->getOperand(i);
                if (arg.is_temp()) {
                    if (arg.kill())
                        colorsUsed[colors[arg.temp()]] = false;
                    arg.setFixed(lir::PhysReg{static_cast<unsigned>(colors[arg.temp()])});
                }
            }
            auto defCount = (*it)->definitionCount();
            for (std::size_t i = 0; i < defCount; ++i) {
                auto& def = (*it)->getDefinition(i);
                if (def.is_temp()) {
                    if (colors[def.temp()] < 0) {
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
                                    if (def.temp() != arg2.temp())
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
                            c = program.temp_info(def.temp()).reg_class == lir::RegClass::vgpr ? 1024 : 0;
                            while (forbidden[c])
                                c += program.temp_info(def.temp()).size;
                        }
                        colorsUsed[c] = true;
                        colors[def.temp()] = c;
                    }
                    def.setFixed(lir::PhysReg{static_cast<unsigned>(colors[def.temp()])});
                }
            }
        }
    }
}

bool
has_logical_phis(lir::Program& program, lir::Block& block)
{
    for (auto it = block.instructions().begin();
         it != block.instructions().end() && (*it)->opCode() == lir::OpCode::phi; ++it)
        if (program.temp_info((*it)->getDefinition(0).temp()).reg_class == lir::RegClass::vgpr)
            return true;
    return false;
}

bool
has_linearized_phis(lir::Program& program, lir::Block& block)
{
    for (auto it = block.instructions().begin();
         it != block.instructions().end() && (*it)->opCode() == lir::OpCode::phi; ++it)
        if (program.temp_info((*it)->getDefinition(0).temp()).reg_class != lir::RegClass::vgpr)
            return true;
    return false;
}

void
destroy_phis(lir::Program& program)
{
    for (auto& bb : program.blocks()) {
        for (auto succ : bb->linearizedSuccessors())
            if (has_linearized_phis(program, *succ))
                std::terminate();

        std::vector<std::pair<lir::Arg, lir::Arg>> args;
        for (auto succ : bb->logicalSuccessors()) {
            auto index = lir::findOrInsertBlock(succ->logicalPredecessors(), bb.get());
            for (auto it = succ->instructions().begin();
                 it != succ->instructions().end() && (*it)->opCode() == lir::OpCode::phi; ++it) {
                if (program.temp_info((*it)->getDefinition(0).temp()).reg_class != lir::RegClass::vgpr)
                    continue;
		args.emplace_back((*it)->getOperand(index), (*it)->getDefinition(0));
            }
        }
        if(args.empty()) continue;

	auto inst = std::make_unique<lir::Inst>(lir::OpCode::parallel_copy, args.size(), args.size());
	for(std::size_t i = 0; i < args.size(); ++i) {
		inst->getOperand(i) = args[i].first;
		inst->getDefinition(i) = args[i].second;
	}

	auto it = --bb->instructions().end();
	bb->instructions().insert(it, std::move(inst));
    }

    for (auto& bb : program.blocks()) {
        auto it = bb->instructions().begin();
        while (it != bb->instructions().end() && (*it)->opCode() == lir::OpCode::phi)
            ++it;
        bb->instructions().erase(bb->instructions().begin(), it);
    }
}

void
allocateRegisters(lir::Program& program)
{
    insertCopies(program);
    fixSSA(program);
    colorRegisters(program);
    destroy_phis(program);
}
}
}
