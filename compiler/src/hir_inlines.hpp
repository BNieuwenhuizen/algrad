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

inline Use::Use(Inst* c) noexcept : consumer_{c}, producer_{nullptr}
{
}

inline Use::Use(Use const& other) noexcept : consumer_{other.consumer_}, producer_{other.producer_}
{
    if (producer_)
        producer_->uses_.push_back(*this);
}

inline Use&
Use::operator=(Use const& other) noexcept
{
    if (producer_)
        producer_->uses_.erase(UseList::s_iterator_to(*this));
    producer_ = other.producer_;
    consumer_ = other.consumer_;
    if (producer_)
        producer_->uses_.push_back(*this);
    return *this;
}

inline Use::~Use() noexcept
{
    if (producer_)
        producer_->uses_.erase(UseList::s_iterator_to(*this));
}

inline Inst*
Use::consumer() noexcept
{
    return consumer_;
}

inline Def*
Use::producer() noexcept
{
    return producer_;
}

inline void
Use::setProducer(Def* def) noexcept
{
    if (producer_)
        producer_->uses_.erase(UseList::s_iterator_to(*this));
    producer_ = def;
    if (producer_)
        producer_->uses_.push_back(*this);
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

inline boost::iterator_range<UseIterator>
Def::uses() noexcept
{
    return {uses_.begin(), uses_.end()};
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
    return operands_.size();
}

inline Def*
Inst::getOperand(std::size_t index) noexcept
{
    return operands_[index].producer();
}

inline void
Inst::setOperand(std::size_t index, Def* def) noexcept
{
    operands_[index].setProducer(def);
}

inline InstFlags
Inst::flags() const noexcept
{
    return flags_;
}

inline BasicBlock*
Inst::parent() noexcept
{
    return parent_;
}

inline void
Inst::setParent(BasicBlock* bb) noexcept
{
    parent_ = bb;
}

inline bool
Inst::isVarying() const noexcept
{
    return !!(flags_ & InstFlags::isVarying);
}

inline void
Inst::markVarying() noexcept
{
    flags_ = (flags_ | InstFlags::isVarying);
}

inline int
BasicBlock::id() const noexcept
{
    return id_;
}

inline void
BasicBlock::setId(int v) noexcept
{
    id_ = v;
}

inline Inst&
BasicBlock::insertFront(std::unique_ptr<Inst> insn) noexcept
{
    insn->setParent(this);
    instructions_.push_front(*insn);
    return *insn.release();
}

inline Inst&
BasicBlock::insertBack(std::unique_ptr<Inst> insn) noexcept
{
    insn->setParent(this);
    instructions_.push_back(*insn);
    return *insn.release();
}

inline Inst&
BasicBlock::insertBefore(Inst& pos, std::unique_ptr<Inst> inst) noexcept
{
    inst->setParent(this);
    instructions_.insert(InstList::s_iterator_to(pos), *inst);
    return *inst.release();
}

inline std::unique_ptr<Inst>
BasicBlock::erase(Inst& inst) noexcept
{
    inst.setParent(nullptr);
    instructions_.erase(InstList::s_iterator_to(inst));
    return std::unique_ptr<Inst>{&inst};
}

inline boost::iterator_range<InstIterator>
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

inline std::vector<std::unique_ptr<BasicBlock>>&
Program::basicBlocks() noexcept
{
    return basicBlocks_;
}

inline boost::iterator_range<InstIterator>
Program::variables() noexcept
{
    return variables_;
}

inline Inst&
Program::insertVariable(std::unique_ptr<Inst> inst) noexcept
{
    variables_.push_back(*inst);
    return *inst.release();
}

inline std::unique_ptr<Inst>
Program::eraseVariable(Inst& inst) noexcept
{
    variables_.erase(InstList::s_iterator_to(inst));
    return std::unique_ptr<Inst>{&inst};
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
