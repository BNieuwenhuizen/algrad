#include "lir.hpp"

#include <algorithm>

namespace algrad {
namespace compiler {

LInst::LInst(LOpCode opCode, unsigned argCount)
  : opCode_{opCode}
  , argCount_{static_cast<std::uint16_t>(argCount)}
{
    if (argCount_ > internalArgCount) {
        externalArgs_ = new LArg[argCount_];
    }
}

LInst::LInst(LInst&& other) noexcept : opCode_{other.opCode_}, argCount_{other.argCount_}
{
    std::copy(other.internalArgs_, other.internalArgs_ + internalArgCount, internalArgs_);
    other.argCount_ = 0;
}

LInst::LInst(LInst const& other)
  : opCode_{other.opCode_}
  , argCount_{other.argCount_}
{
    if (argCount_ > internalArgCount) {
        externalArgs_ = new LArg[argCount_];
        std::copy(other.externalArgs_, other.externalArgs_ + argCount_, externalArgs_);
    } else {
        std::copy(other.internalArgs_, other.internalArgs_ + internalArgCount, internalArgs_);
    }
}

LInst&
LInst::operator=(LInst&& other) noexcept
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

LInst&
LInst::operator=(LInst const& other)
{
    if (this != &other) {
        LArg* newArgs;
        if (other.argCount_ > internalArgCount)
            newArgs = new LArg[other.argCount_];
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

LInst::~LInst() noexcept
{
    if (argCount_ > internalArgCount)
        delete[] externalArgs_;
}

LProgram::LProgram()
  : nextId_{0}
{
}

char const*
toString(LOpCode op)
{
    switch (op) {
#define HANDLE(v)                                                                                                      \
    case LOpCode::v:                                                                                                   \
        return #v;
        ALGRAD_COMPILER_LIR_OPCODES(HANDLE)
#undef HANDLE
    }
}

void
print(std::ostream& os, LProgram& program)
{
    os << "----- lprogram -----\n";
    for (auto& fun : program.functions()) {
        os << "  func\n";
        for (auto& bb : fun->blocks()) {
            os << "    block\n";
            for(auto& insn : bb->instructions()) {
                os << "      " << toString(insn.opCode());
                for(auto arg : insn.args()) {
                    os << "   ";
                    if(arg.role() == LArg::Role::constant)
                        os << std::hex << "0x" << arg.data() << std::dec;
                    else {
                        os << "t" << arg.data() << "_" << (arg.regClass() == RegClass::vgpr ? "v" : "s") << (arg.size() * 8);
                        if(arg.isFixed()) {
                            os << "(";
                            if(arg.physReg().reg >= 256)
                                os << "v" << (arg.physReg().reg - 256);
                            else if(arg.physReg().reg < 102)
                                os << "s" << (arg.physReg().reg);
                            else if(arg.physReg().reg == 124)
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