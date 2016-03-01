#include "lir.hpp"

#include <algorithm>

namespace algrad {
namespace compiler {
namespace lir {
Inst::Inst(OpCode opCode, unsigned argCount)
  : opCode_{opCode}
  , argCount_{static_cast<std::uint16_t>(argCount)}
{
    if (argCount_ > internalArgCount) {
        externalArgs_ = new Arg[argCount_];
    }
}

Inst::Inst(Inst&& other) noexcept : opCode_{other.opCode_}, argCount_{other.argCount_}
{
    std::copy(other.internalArgs_, other.internalArgs_ + internalArgCount, internalArgs_);
    other.argCount_ = 0;
}

Inst::Inst(Inst const& other)
  : opCode_{other.opCode_}
  , argCount_{other.argCount_}
{
    if (argCount_ > internalArgCount) {
        externalArgs_ = new Arg[argCount_];
        std::copy(other.externalArgs_, other.externalArgs_ + argCount_, externalArgs_);
    } else {
        std::copy(other.internalArgs_, other.internalArgs_ + internalArgCount, internalArgs_);
    }
}

Inst&
Inst::operator=(Inst&& other) noexcept
{
    if (this != &other) {
        if (argCount_ > internalArgCount)
            delete[] externalArgs_;
        opCode_ = other.opCode_;
        argCount_ = other.argCount_;
        std::copy(other.internalArgs_, other.internalArgs_ + internalArgCount, internalArgs_);
        other.argCount_ = 0;
    }
    return *this;
}

Inst&
Inst::operator=(Inst const& other)
{
    if (this != &other) {
        Arg* newArgs;
        if (other.argCount_ > internalArgCount)
            newArgs = new Arg[other.argCount_];
        if (argCount_ > internalArgCount)
            delete[] externalArgs_;
        opCode_ = other.opCode_;
        argCount_ = other.argCount_;
        if (argCount_ > internalArgCount) {
            externalArgs_ = newArgs;
            std::copy(other.externalArgs_, other.externalArgs_ + argCount_, externalArgs_);
        } else
            std::copy(other.internalArgs_, other.internalArgs_ + internalArgCount, internalArgs_);
    }
    return *this;
}

Inst::~Inst() noexcept
{
    if (argCount_ > internalArgCount)
        delete[] externalArgs_;
}

Program::Program()
  : nextId_{0}
{
}

char const*
toString(OpCode op)
{
    switch (op) {
#define HANDLE(v)                                                                                                      \
    case OpCode::v:                                                                                                   \
        return #v;
        ALGRAD_COMPILER_LIR_OPCODES(HANDLE)
#undef HANDLE
    }
}

void
print(std::ostream& os, Program& program)
{
    os << "----- lprogram -----\n";
    for (auto& bb : program.blocks()) {
        os << "    block\n";
        for (auto& insn : bb->instructions()) {
            os << "      " << toString(insn.opCode());
            for (auto arg : insn.args()) {
                os << "   ";
                if (arg.role() == Arg::Role::constant)
                    os << std::hex << "0x" << arg.data() << std::dec;
                else {
                    os << "t" << arg.data() << "_" << (arg.regClass() == RegClass::vgpr ? "v" : "s")
                       << (arg.size() * 8);
                    if (arg.isFixed()) {
                        os << "(";
                        if (arg.physReg().reg >= 256)
                            os << "v" << (arg.physReg().reg - 256);
                        else if (arg.physReg().reg < 102)
                            os << "s" << (arg.physReg().reg);
                        else if (arg.physReg().reg == 124)
                            os << "m0";
                        os << ")";
                    }
                }
            }
            os << "\n";
        }
    }
}
}
}
}
