#include "ir.hpp"

#include <iostream>

namespace algrad {
namespace compiler {

Def::~Def() noexcept
{
}

Instruction::Instruction(OpCode opCode, int id, Type type, unsigned operandCount) noexcept : Def{opCode, id, type}
{
    operandCount_ = operandCount;
    if (operandCount_ > internalOperandCount)
        externalOperands_ = new Def*[operandCount_];
}

Instruction::Instruction(Instruction const& other)
  : Def{other}
{
    if(operandCount_ > internalOperandCount)
        externalOperands_ = new Def*[operandCount_];

    auto src = operandCount_ > internalOperandCount ? other.externalOperands_ : other.internalOperands_;
    auto dest = operandCount_ > internalOperandCount ? externalOperands_ : internalOperands_;
    std::copy(src, src + operandCount_, dest);
}

Instruction& Instruction::operator=(Instruction&& other) {
    if(this != &other) {
        if(operandCount_ > internalOperandCount)
            delete[] externalOperands_;
        Def::operator=(other);
        if(operandCount_ > internalOperandCount) {
            externalOperands_ = other.externalOperands_;
            other.operandCount_ = 0;
        } else {
            std::copy(other.internalOperands_, other.internalOperands_ + internalOperandCount, internalOperands_);
        }
    }
    return *this;
}

Instruction::~Instruction() noexcept
{
    if (operandCount_ > internalOperandCount)
        delete[] externalOperands_;
}

void
Instruction::identify(Def* def)
{
    if (operandCount_ > internalOperandCount)
        delete[] externalOperands_;
    operandCount_ = 1;
    internalOperands_[0] = def;
    opCode_ = OpCode::identity;
}

void
Instruction::eraseOperand(unsigned index) noexcept
{
    auto op = operands();
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

Instruction&
BasicBlock::insertBack(std::unique_ptr<Instruction> insn)
{
    auto& ret = *insn;
    instructions_.push_back(std::move(insn));
    return ret;
}

Function::Function(int id, Type type)
  : Def{OpCode::function, id, type}
{
}

Function::~Function() noexcept
{
}

BasicBlock&
Function::insertBack(std::unique_ptr<BasicBlock> bb)
{
    auto& ret = *bb;
    basicBlocks_.push_back(std::move(bb));
    return ret;
}

BasicBlock&
Function::insertFront(std::unique_ptr<BasicBlock> bb)
{
    auto& ret = *bb;
    basicBlocks_.insert(basicBlocks_.begin(), std::move(bb));
    return ret;
}

std::unique_ptr<BasicBlock>
Function::erase(BasicBlock& bb)
{
    for (auto it = basicBlocks_.begin(); it != basicBlocks_.end(); ++it) {
        if (it->get() == &bb) {
            auto ret = std::move(*it);
            basicBlocks_.erase(it);
            return ret;
        }
    }
    assert(0 && "BasicBlock not found during erase");
}

Instruction&
Function::appendParam(std::unique_ptr<Instruction> param)
{
    auto& ret = *param;
    params_.push_back(std::move(param));
    return ret;
}

Program::Program(ProgramType type)
  : type_{type}
  , nextDefIndex_{0}
  , nextBlockIndex_{0}
  , mainFunction_{nullptr}
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

Function*
Program::createFunction(Type type)
{
    auto f = std::make_unique<Function>(nextDefIndex_++, type);
    auto ret = f.get();
    functions_.push_back(std::move(f));
    return ret;
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
#define HANDLE(v)                                                                                                      \
    case OpCode::v:                                                                                                    \
        return #v;
        ALGRAD_COMPILER_OPCODES(HANDLE)
#undef HANDLE
    }
}

void
print(std::ostream& os, Function& function)
{
    os << "  fun %" << function.id() << "(";
    for (auto& p : function.params()) {
        os << " %" << p->id();
    }
    os << " )\n";
    for (auto& insn : function.variables()) {
        os << "      ";
        os << "%" << insn->id() << " = " << toString(insn->opCode());
        os << "\n";
    }
    for (auto& bb : function.basicBlocks()) {
        os << "    block " << bb->id() << ":\n";
        for (auto& insn : bb->instructions()) {
            os << "      ";
            if (insn->type() != &voidType)
                os << "%" << insn->id() << " = ";
            os << toString(insn->opCode());
            for (auto e : insn->operands()) {
                if (e->opCode() == OpCode::constant && e->type()->kind() == TypeKind::integer)
                    os << " " << static_cast<ScalarConstant const*>(e)->integerValue();
                else
                    os << " %" << e->id();
            }
            os << "\n";
        }
    }
}

void
print(std::ostream& os, Program& program)
{
    os << "----- program(" << program.type() << ") ----\n";

    for (auto& f : program.functions()) {
        print(os, *f);
    }
}
}
}
