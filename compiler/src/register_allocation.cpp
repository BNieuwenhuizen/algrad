#include "lir.hpp"

#include <iostream>
#include <unordered_map>
#include <unordered_set>

#include <boost/range/adaptor/reversed.hpp>

namespace algrad {
namespace compiler {

using Live_set = std::unordered_set<unsigned>;

Live_set
get_live_out(std::vector<Live_set>& liveIn, lir::Program const& program, lir::Block& bb)
{
    Live_set ret;
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
std::vector<Live_set>
compute_live_in(lir::Program& program)
{
    std::vector<Live_set> live_in(program.blocks().size());

    for (;;) {
        bool changed = false;
        for (auto& bb : boost::adaptors::reverse(program.blocks())) {
            auto live = get_live_out(live_in, program, *bb);
            for (auto& insn : boost::adaptors::reverse(bb->instructions())) {
                auto defCount = insn->definitionCount();
                for (unsigned i = 0; i < defCount; ++i) {
                    auto it = live.find(insn->getDefinition(i).temp());
                    if (it != live.end())
                        live.erase(it);
                }

                if (insn->opCode() != lir::OpCode::phi) {
                    auto op_count = insn->operandCount();
                    for (unsigned i = 0; i < op_count; ++i) {
                        if (insn->getOperand(i).is_temp()) {
                            insn->getOperand(i).setKill(live.find(insn->getOperand(i).temp()) == live.end());
                        }
                    }
                    for (unsigned i = 0; i < op_count; ++i) {
                        if (insn->getOperand(i).is_temp()) {
                            live.insert(insn->getOperand(i).temp());
                        }
                    }
                }
            }
            if (live != live_in[bb->id()]) {
                live_in[bb->id()] = live;
                changed = true;
            }
        }
        if (!changed)
            break;
    }
    return live_in;
}

void
insert_copies(lir::Program& program)
{
    auto live_in = compute_live_in(program);
    for (auto& bb : program.blocks()) {
        std::vector<std::unique_ptr<lir::Inst>> instructions;
        instructions.reserve(bb->instructions().size());
        auto live = get_live_out(live_in, program, *bb);

        for (auto it = bb->instructions().end(); it != bb->instructions().begin();) {
            --it;
            auto& insn = *it;
            bool need_move = false;
            auto def_count = insn->definitionCount();
            for (std::size_t i = 0; i < def_count; ++i) {
                auto const& def = insn->getDefinition(i);
                if (def.is_temp() && def.isFixed())
                    need_move = true;
                auto it = live.find(def.temp());
                if (it != live.end())
                    live.erase(it);
            }

            auto op_count = insn->operandCount();
            for (std::size_t i = 0; i < op_count; ++i) {
                auto arg = insn->getOperand(i);
                if (arg.is_temp()) {
                    if (arg.isFixed())
                        need_move = true;
                    live.insert(arg.temp());
                }
            }

            instructions.push_back(std::move(insn));
            if (need_move && !live.empty()) {
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
fix_ssa_rename_visit(lir::Program& program, lir::Block& block, std::vector<bool>& visited,
                     std::vector<unsigned>& renames, std::vector<std::pair<unsigned, unsigned>>& undo, bool logical)
{
    if (visited[block.id()])
        return;
    visited[block.id()] = true;

    for (auto& insn : block.instructions()) {
        if (insn->opCode() != lir::OpCode::phi) {
            auto op_count = insn->operandCount();
            for (std::size_t i = 0; i < op_count; ++i) {
                auto id = insn->getOperand(i).temp();
                if ((logical && program.temp_info(id).reg_class != lir::RegClass::vgpr) ||
                    (!logical && program.temp_info(id).reg_class == lir::RegClass::vgpr))
                    continue;
                if (renames[id] == ~0U)
                    std::terminate();

                insn->getOperand(i).set_temp(renames[id]);
            }
        }

        auto def_count = insn->definitionCount();
        for (std::size_t i = 0; i < def_count; ++i) {
            auto id = insn->getDefinition(i).temp();
            if ((logical && program.temp_info(id).reg_class != lir::RegClass::vgpr) ||
                (!logical && program.temp_info(id).reg_class == lir::RegClass::vgpr))
                continue;
            undo.push_back({id, renames[id]});
            if (renames[id] == ~0U) {
                renames[id] = id;
            } else {
                auto new_temp = program.allocate_temp(program.temp_info(id).reg_class, program.temp_info(id).size);
                renames[id] = new_temp;
                insn->getDefinition(i).set_temp(new_temp);
            }
        }
    }

    auto undo_size = undo.size();
    if (logical) {
        for (auto succ : block.logicalSuccessors())
            fix_ssa_rename_visit(program, *succ, visited, renames, undo, logical);
    } else {
        for (auto succ : block.linearizedSuccessors())
            fix_ssa_rename_visit(program, *succ, visited, renames, undo, logical);
    }

    for (std::size_t i = undo.size(); i > undo_size; --i) {
        renames[undo[i - 1].first] = undo[i - 1].second;
    }

    undo.resize(undo_size);
}

void
fix_ssa_rename(lir::Program& program)
{
    std::vector<bool> visited(program.blocks().size());
    std::vector<unsigned> renames(program.allocated_temp_count(), ~0U);
    std::vector<std::pair<unsigned, unsigned>> undo;
    fix_ssa_rename_visit(program, *program.blocks()[0], visited, renames, undo, false);
    std::fill(visited.begin(), visited.end(), false);
    fix_ssa_rename_visit(program, *program.blocks()[0], visited, renames, undo, true);
}
void
fix_ssa(lir::Program& program)
{
    fix_ssa_rename(program);
}

std::vector<int>
color_registers(lir::Program& program)
{
    std::vector<std::unordered_set<unsigned>> ig(program.allocated_temp_count());
    std::vector<int> colors(program.allocated_temp_count(), -1);
    auto live_in = compute_live_in(program);

    for (auto& bb : program.blocks()) {
        std::vector<bool> colors_used(2048);
        for (auto e : live_in[bb->id()]) {
            auto size = program.temp_info(e).size;
            for (std::size_t i = 0; i < size; ++i)
                colors_used[colors[e] + i] = true;
        }
        for (auto it = bb->instructions().begin(); it != bb->instructions().end(); ++it) {
            if ((*it)->opCode() != lir::OpCode::phi) {
                auto op_count = (*it)->operandCount();
                for (std::size_t i = 0; i < op_count; ++i) {
                    auto& arg = (*it)->getOperand(i);
                    if (arg.is_temp()) {
                        if (arg.kill()) {
                            auto size = program.temp_info(arg.temp()).size;
                            for (std::size_t j = 0; j < size; ++j)
                                colors_used[colors[arg.temp()] + j] = false;
                        }
                        arg.setFixed(lir::PhysReg{static_cast<unsigned>(colors[arg.temp()])});
                    }
                }
            }

            auto def_count = (*it)->definitionCount();
            for (std::size_t i = 0; i < def_count; ++i) {
                auto& def = (*it)->getDefinition(i);
                if (def.is_temp()) {
                    if (colors[def.temp()] < 0) {
                        std::vector<bool> forbidden = colors_used;
                        int c = -1;
                        if (def.isFixed()) {
                            c = def.physReg().reg;
                        }
                        if (it + 1 != bb->instructions().end() && (*it)->opCode() == lir::OpCode::parallel_copy) {
                            auto op_count_2 = it[1]->operandCount();
                            for (unsigned j = 0; j < op_count_2; ++j) {
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
                            auto prev_arg = (*it)->getOperand(i);
                            if (!forbidden[prev_arg.physReg().reg]) {
                                c = prev_arg.physReg().reg;
                            }
                        }

                        if (c == -1) {
                            c = program.temp_info(def.temp()).reg_class == lir::RegClass::vgpr ? 1024 : 0;
                            while (forbidden[c])
                                c += program.temp_info(def.temp()).size;
                        }

                        auto size = program.temp_info(def.temp()).size;
			for(std::size_t j = 0; j < size; ++j)
				colors_used[c + j] = true;
                        colors[def.temp()] = c;
                    }
                    def.setFixed(lir::PhysReg{static_cast<unsigned>(colors[def.temp()])});
                }
            }
        }
    }
    for (auto& bb : program.blocks()) {
        for (auto it = bb->instructions().begin();
             it != bb->instructions().end() && (*it)->opCode() == lir::OpCode::phi; ++it) {
            auto op_count = (*it)->operandCount();
            for (std::size_t i = 0; i < op_count; ++i) {
                auto& arg = (*it)->getOperand(i);
                if (arg.is_temp()) {
                    arg.setFixed(lir::PhysReg{static_cast<unsigned>(colors[arg.temp()])});
                }
            }
        }
    }
    return colors;
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
        if (args.empty())
            continue;

        auto inst = std::make_unique<lir::Inst>(lir::OpCode::parallel_copy, args.size(), args.size());
        for (std::size_t i = 0; i < args.size(); ++i) {
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
    insert_copies(program);
    fix_ssa(program);
    color_registers(program);
    destroy_phis(program);
}
}
}
