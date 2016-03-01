#ifndef ALGRAD_HIR_INLINES_HPP
#define ALGRAD_HIR_INLINES_HPP

#include "hir.hpp"

namespace algrad {
namespace compiler {
namespace hir {

constexpr InstFlags
operator|(InstFlags a, InstFlags b) noexcept
{
    return static_cast<InstFlags>(static_cast<std::uint16_t>(a) | static_cast<std::uint16_t>(b));
}

constexpr InstFlags operator&(InstFlags a, InstFlags b) noexcept
{
    return static_cast<InstFlags>(static_cast<std::uint16_t>(a) & static_cast<std::uint16_t>(b));
}

constexpr InstFlags operator~(InstFlags flags) noexcept
{
    return static_cast<InstFlags>(0xFFFF & ~static_cast<std::uint16_t>(flags));
}

constexpr bool operator!(InstFlags flags) noexcept
{
    return flags == InstFlags::none;
}

inline Def::Def(OpCode opCode, int id, Type type) noexcept : opCode_{opCode}, id_{id}, type_{type}
{
}

inline OpCode
Def::opCode() const noexcept
{
    return opCode_;
}

inline Type
Def::type() const noexcept
{
    return type_;
}

inline int
Def::id() const noexcept
{
    return id_;
}

inline ScalarConstant::ScalarConstant(OpCode opCode, int id, Type type, std::uint64_t v) noexcept
  : Def{opCode, id, type}
{
    integerValue_ = v;
}

inline ScalarConstant::ScalarConstant(OpCode opCode, int id, Type type, double v) noexcept : Def{opCode, id, type}
{
    floatValue_ = v;
}

inline double
ScalarConstant::floatValue() const noexcept
{
    return floatValue_;
}

inline std::uint64_t
ScalarConstant::integerValue() const noexcept
{
    return integerValue_;
}

inline std::size_t
Inst::operandCount() const noexcept
{
    return operandCount_;
}

inline Def*
Inst::getOperand(std::size_t index) const noexcept
{
    if (operandCount_ <= internalOperandCount)
        return internalOperands_[index];
    else
        return externalOperands_[index];
}

inline void
Inst::setOperand(std::size_t index, Def* def) noexcept
{
    if (operandCount_ <= internalOperandCount)
        internalOperands_[index] = def;
    else
        externalOperands_[index] = def;
}

inline InstFlags
Inst::flags() const noexcept
{
    return flags_;
}

inline int
BasicBlock::id() const noexcept
{
    return id_;
}

inline std::vector<std::unique_ptr<Inst>>&
BasicBlock::instructions() noexcept
{
    return instructions_;
}

inline std::vector<BasicBlock*>&
BasicBlock::successors() noexcept
{
    return successors_;
}

inline std::vector<BasicBlock*> const&
BasicBlock::predecessors() noexcept
{
    return predecessors_;
}

inline ProgramType
Program::type() const noexcept
{
    return type_;
}

template <typename T, typename... Args>
std::unique_ptr<T>
Program::createDef(OpCode opCode, Type type, Args&&... args)
{
    return std::make_unique<T>(opCode, nextDefIndex_++, type, std::forward<Args>(args)...);
}

inline TypeContext&
Program::types() noexcept
{
    return types_;
}

inline unsigned
Program::defIdCount() const noexcept
{
    return nextDefIndex_;
}

inline std::vector<std::unique_ptr<BasicBlock>> const&
Program::basicBlocks() noexcept
{
    return basicBlocks_;
}

inline std::vector<std::unique_ptr<Inst>>&
Program::variables() noexcept
{
    return variables_;
}

inline std::vector<std::unique_ptr<Inst>>&
Program::params() noexcept
{
    return params_;
}

inline BasicBlock&
Program::initialBlock() noexcept
{
    return *basicBlocks_.front();
}
}
}
}
#endif
