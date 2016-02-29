#include "lir.hpp"

#include <unordered_map>
#include <unordered_set>

namespace algrad {
namespace compiler {

void
insertCopies(LProgram& program)
{
    for (auto& func : program.functions()) {
        for (auto& bb : func->blocks()) {
            std::vector<LInst> instructions;
            instructions.reserve(bb->instructions().size());
            std::unordered_map<unsigned, LReg> live;
            for (auto it = bb->instructions().end(); it != bb->instructions().begin();) {
                --it;
                LInst& insn = *it;
                bool needMove = true;
                for (auto const& arg : insn.args()) {
                    if (arg.role() == LArg::Role::def) {
                        if (arg.isFixed())
                            needMove = true;
                        auto it = live.find(arg.data());
                        if (it != live.end())
                            live.erase(it);
                    }
                }

                for (auto const& arg : insn.args()) {
                    if (arg.role() == LArg::Role::use) {
                        if (arg.isFixed())
                            needMove = true;
                        live[arg.data()] = LReg{arg.data(), arg.regClass(), arg.size()};
                    }
                }

                instructions.push_back(std::move(insn));
                if (needMove) {
                    LInst inst{LOpCode::parallel_copy, static_cast<unsigned>(live.size() * 2)};
                    unsigned idx = 0;
                    for (auto e : live) {
                        inst.args()[idx] = LArg{e.second, LArg::Role::use};
                        inst.args()[idx + 1] = LArg{e.second, LArg::Role::def};
                        idx += 2;
                    }
                    instructions.push_back(std::move(inst));
                }
            }

            std::reverse(instructions.begin(), instructions.end());
            bb->instructions() = std::move(instructions);
        }
    }
}

void
fixSSA(LProgram& program)
{
    for (auto& func : program.functions()) {
        for (auto& bb : func->blocks()) {
            std::unordered_map<std::uint32_t, std::uint32_t> renames;
            for (auto& insn : bb->instructions()) {

                for (auto& arg : insn.args()) {
                    if (arg.role() == LArg::Role::use) {
                        auto it = renames.find(arg.data());
                        if (it != renames.end())
                            arg.data(it->second);
                    }
                }

                for (auto& arg : insn.args()) {
                    if (arg.role() == LArg::Role::def) {
                        auto it = renames.find(arg.data());
                        if (it == renames.end()) {
                            renames.insert({arg.data(), arg.data()});
                        } else {
                            auto next = program.allocateId();
                            it->second = next;
                            arg.data(next);
                        }
                    }
                }
            }
        }
    }
}

void
colorRegisters(LProgram& program)
{
    std::vector<std::unordered_set<unsigned>> ig(program.allocatedIds());
    std::vector<int> colors(program.allocatedIds(), -1);


    for (auto& fun : program.functions()) {
        for (auto& bb : fun->blocks()) {
            std::unordered_map<unsigned, LReg> live;
            for (auto it = bb->instructions().rbegin(); it != bb->instructions().rend(); ++it) {
                LInst& inst = *it;
                for (auto const& arg : inst.args()) {
                    if (arg.role() == LArg::Role::def) {
                        auto it = live.find(arg.data());
                        if (it != live.end())
                            live.erase(it);
                    }
                }

                for (auto& arg : inst.args()) {
                    if (arg.role() == LArg::Role::use) {
                        if (live.find(arg.data()) == live.end())
                            arg.setKill(live.find(arg.data()) == live.end());
                    }
                }

                for (auto const& arg : inst.args()) {
                    if (arg.role() == LArg::Role::use) {
                        live[arg.data()] = LReg{arg.data(), arg.regClass(), arg.size()};
                    }
                }
            }
        }
    }
    for (auto& fun : program.functions()) {
        for (auto& bb : fun->blocks()) {
            std::vector<bool> colorsUsed(512);
            for (auto it = bb->instructions().begin(); it != bb->instructions().end(); ++it) {
                for(auto& arg : it->args()) {
                    if(arg.role() == LArg::Role::use ) {
                        if (arg.kill())
                            colorsUsed[colors[arg.data()]] = false;
                        arg.setFixed(PhysReg{colors[arg.data()]});
                    }
                }
                LArg prevArg;
                for(auto& arg : it->args()) {
                    if(arg.role() == LArg::Role::def && colors[arg.data()] < 0) {
                        std::vector<bool> forbidden = colorsUsed;
                        int c =  -1;
                        if(arg.isFixed()) {
                            c = arg.physReg().reg;
                        }
                        if(it + 1 != bb->instructions().end()) {
                            for(auto const& arg2 : it[1].args())  {
                                if(arg2.isFixed()) {
                                    if(arg.data() != arg2.data())
                                        forbidden[arg2.physReg().reg] = true;
                                    else
                                        c = arg2.physReg().reg;
                                }
                            }
                        }
                        if(c == -1 && it->opCode() == LOpCode::parallel_copy && !forbidden[prevArg.physReg().reg]) {
                            c = prevArg.physReg().reg;
                        }

                        if(c == -1) {
                            c =  arg.regClass() == RegClass::vgpr ? 256 : 0;
                            while (forbidden[c]) ++c;
                        }
                        colorsUsed[c] = true;
                        colors[arg.data()] = c;
                        arg.setFixed(PhysReg{c});
                    }
                    prevArg = arg;
                }
            }
        }
    }
}

void
allocateRegisters(LProgram& program)
{
    insertCopies(program);
    fixSSA(program);
    colorRegisters(program);
}
}
}