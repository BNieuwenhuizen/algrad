#include "hir.hpp"
#include "hir_inlines.hpp"

#include <iostream>

namespace algrad {
namespace compiler {
namespace hir {

InstFlags const defaultInstFlags[] = {
#define HANDLE(name, value) value,
  ALGRAD_COMPILER_HIR_OPCODES(HANDLE)
#undef HANDLE
};

Def::~Def() noexcept
{
}

Inst::Inst(OpCode opCode, int id, Type type, unsigned operandCount) noexcept
  : Def{opCode, id, type},
    flags_{defaultInstFlags[static_cast<std::uint16_t>(opCode)]}
{
    operandCount_ = operandCount;
    if (operandCount_ > internalOperandCount)
        externalOperands_ = new Def*[operandCount_];
}

Inst::Inst(OpCode opCode, int id, Type type, InstFlags flags, unsigned operandCount) noexcept : Def{opCode, id, type},
                                                                                                flags_{flags}
{
    operandCount_ = operandCount;
    if (operandCount_ > internalOperandCount)
        externalOperands_ = new Def*[operandCount_];
}

Inst::Inst(Inst const& other)
  : Def{other}
{
    if (operandCount_ > internalOperandCount)
        externalOperands_ = new Def*[operandCount_];

    auto src = operandCount_ > internalOperandCount ? other.externalOperands_ : other.internalOperands_;
    auto dest = operandCount_ > internalOperandCount ? externalOperands_ : internalOperands_;
    std::copy(src, src + operandCount_, dest);
}

Inst&
Inst::operator=(Inst&& other)
{
    if (this != &other) {
        if (operandCount_ > internalOperandCount)
            delete[] externalOperands_;
        Def::operator=(other);
        if (operandCount_ > internalOperandCount) {
            externalOperands_ = other.externalOperands_;
            other.operandCount_ = 0;
        } else {
            std::copy(other.internalOperands_, other.internalOperands_ + internalOperandCount, internalOperands_);
        }
    }
    return *this;
}

Inst::~Inst() noexcept
{
    if (operandCount_ > internalOperandCount)
        delete[] externalOperands_;
}

void
Inst::identify(Def* def)
{
    if (operandCount_ > internalOperandCount)
        delete[] externalOperands_;
    operandCount_ = 1;
    internalOperands_[0] = def;
    opCode_ = OpCode::identity;
}

void
Inst::eraseOperand(unsigned index) noexcept
{
    auto op = operandCount_ <= internalOperandCount ? internalOperands_ : externalOperands_;
    for (unsigned idx = index; idx + 1 < operandCount_; ++idx)
        op[idx] = op[idx + 1];
    --operandCount_;
    if (operandCount_ == 3) {
        std::copy(externalOperands_, externalOperands_ + 3, internalOperands_);
    }
}

BasicBlock::BasicBlock(int id)
  : id_{id}
{
}

Inst&
BasicBlock::insertBack(std::unique_ptr<Inst> insn)
{
    auto& ret = *insn;
    instructions_.push_back(std::move(insn));
    return ret;
}


std::size_t BasicBlock::insertPredecessor(BasicBlock* pred) {
	for(std::size_t i = 0; i < predecessors_.size(); ++i)
		if(predecessors_[i] == pred)
			return i;
	predecessors_.push_back(pred);
	return predecessors_.size() - 1;
}
Program::Program(ProgramType type)
  : type_{type}
  , nextDefIndex_{0}
  , nextBlockIndex_{0}
{
}

Program::~Program() noexcept
{
}

std::unique_ptr<BasicBlock>
Program::createBasicBlock()
{
    return std::make_unique<BasicBlock>(nextBlockIndex_++);
}

ScalarConstant*
Program::getScalarConstant(Type type, std::uint64_t v)
{
    for (auto& e : scalarConstants_) {
        if (type == e->type() && e->integerValue() == v)
            return e.get();
    }
    scalarConstants_.push_back(createDef<ScalarConstant>(OpCode::constant, type, v));
    return scalarConstants_.back().get();
}

ScalarConstant*
Program::getScalarConstant(Type type, double v)
{
    for (auto& e : scalarConstants_) {
        if (type == e->type() && e->integerValue() == v)
            return e.get();
    }
    scalarConstants_.push_back(createDef<ScalarConstant>(OpCode::constant, type, v));
    return scalarConstants_.back().get();
}

BasicBlock&
Program::insertBack(std::unique_ptr<BasicBlock> bb)
{
    auto& ret = *bb;
    basicBlocks_.push_back(std::move(bb));
    return ret;
}

Inst&
Program::appendParam(std::unique_ptr<Inst> param)
{
    auto& ret = *param;
    params_.push_back(std::move(param));
    return ret;
}

std::ostream&
operator<<(std::ostream& os, ProgramType type)
{
    switch (type) {
        case ProgramType::fragment:
            os << "fragment";
            break;
        case ProgramType::vertex:
            os << "vertex";
            break;
        case ProgramType::compute:
            os << "compute";
            break;
    }
    return os;
}

char const*
toString(OpCode op)
{
    switch (op) {
#define HANDLE(v, dummy)                                                                                               \
    case OpCode::v:                                                                                                    \
        return #v;
        ALGRAD_COMPILER_HIR_OPCODES(HANDLE)
#undef HANDLE
    }
}

void
print(std::ostream& os, Program& program)
{
    os << "----- program(" << program.type() << ") ----\n";

    os << "  params ";
    for (auto& p : program.params()) {
        os << " %" << p->id();
    }
    os << ")\n";
    for (auto& insn : program.variables()) {
        os << "    ";
        os << "%" << insn->id() << " = " << toString(insn->opCode());
        os << "\n";
    }
    for (auto& bb : program.basicBlocks()) {
        os << "  block " << bb->id() << ":\n";
        for (auto& insn : bb->instructions()) {
            os << "     ";
            if (insn->type() != &voidType)
                os << "%" << insn->id() << " = ";
            os << toString(insn->opCode());
            std::size_t operandCount = insn->operandCount();
            for (std::size_t i = 0; i < operandCount; ++i) {
                auto op = insn->getOperand(i);
                if (op->opCode() == OpCode::constant && op->type()->kind() == TypeKind::integer)
                    os << " " << static_cast<ScalarConstant const*>(op)->integerValue();
                else
                    os << " %" << op->id();
            }
            os << "\n";
        }
        os << "    successors";
	for(auto succ : bb->successors()) {
		os << " " << succ->id();
	}
	os << "\n";
    }
}
}
}
}
