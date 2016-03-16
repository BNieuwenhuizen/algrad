#include "lir.hpp"

#include <algorithm>

namespace algrad {
namespace compiler {
namespace lir {

Inst::Inst(OpCode opCode, std::size_t defCount, std::size_t opCount) noexcept
  : opCode_{opCode},
    defCount_{static_cast<std::uint16_t>(defCount)},
    opCount_{static_cast<std::uint16_t>(opCount)}
{
    assert(defCount <= std::numeric_limits<std::uint16_t>::max());
    assert(opCount <= std::numeric_limits<std::uint16_t>::max());
    if (defCount_ + opCount_ > internalArgCount_)
        externalArgs_ = new Arg[defCount + opCount_];
}

Inst::~Inst() noexcept
{
    if (defCount_ + opCount_ > internalArgCount_)
        delete[] externalArgs_;
}

Program::Program()
{
}

char const*
toString(OpCode op)
{
    switch (op) {
#define HANDLE(v)                                                                                                      \
    case OpCode::v:                                                                                                    \
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
            os << "      " << insn.get() << " " << toString(insn->opCode()) << " ";

	    for(std::size_t i = 0; i < insn->definitionCount(); ++i) {
		    if(i) os << ", ";
		    auto arg = insn->getDefinition(i);
		    if(arg.is_temp()) {
			    os << arg.temp();
		    }
	    }
	    os << " <- ";
	    for(std::size_t i = 0; i < insn->operandCount(); ++i) {
		    if(i) os << ", ";
		    auto arg = insn->getOperand(i);
		    if(arg.is_temp()) {
			    os << arg.temp();
			    if(arg.kill())
				    os << "(k)";
		    }
	    }

            os << "\n";
        }
    }
}
}
}
}
