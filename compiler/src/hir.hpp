#ifndef ALGRAD_COMPILER_IR_HPP
#define ALGRAD_COMPILER_IR_HPP

#include <cstdint>
#include <iosfwd>
#include <memory>

#include "types.hpp"

#include <boost/intrusive/list.hpp>
#include <boost/intrusive/list_hook.hpp>
#include <boost/range/iterator_range.hpp>

namespace algrad {
namespace compiler {

namespace hir {

#define ALGRAD_COMPILER_HIR_OPCODES(_)                                                                                 \
    _(constant, InstFlags::none)                                                                                       \
    _(parameter, InstFlags::none)                                                                                      \
    _(variable, InstFlags::none)                                                                                       \
    _(phi, InstFlags::none)                                                                                            \
    _(ret, InstFlags::isControlInstruction)                                                                            \
    _(branch, InstFlags::isControlInstruction)                                                                         \
    _(condBranch, InstFlags::isControlInstruction)                                                                     \
    _(accessChain, InstFlags::none)                                                                                    \
    _(load, InstFlags::none)                                                                                           \
    _(store, InstFlags::hasSideEffects)                                                                                \
    _(compositeConstruct, InstFlags::none)                                                                             \
    _(compositeExtract, InstFlags::none)                                                                               \
    _(vectorShuffle, InstFlags::none)                                                                                  \
    _(orderedLessThan, InstFlags::none)                                                                                \
    _(gcnInterpolate, InstFlags::none)                                                                                 \
    _(gcnExport, InstFlags::hasSideEffects)

enum class InstFlags : std::uint16_t
{
    none = 0,
    hasSideEffects = 1U << 0,
    isControlInstruction = 1U << 1
};

constexpr InstFlags operator|(InstFlags, InstFlags) noexcept;
constexpr InstFlags operator&(InstFlags, InstFlags) noexcept;
constexpr InstFlags operator~(InstFlags) noexcept;
constexpr bool operator!(InstFlags) noexcept;

enum class OpCode : std::uint16_t
{
#define HANDLE(v, dummy) v,
    ALGRAD_COMPILER_HIR_OPCODES(HANDLE)
#undef HANDLE
};

extern InstFlags const defaultInstFlags[];

class Def;
class Inst;

class Use final : public boost::intrusive::list_base_hook<>
{
  public:
    explicit Use(Inst*) noexcept;
    Use(Use const&) noexcept;
    Use& operator=(Use const&) noexcept;
    ~Use() noexcept;

    Inst* consumer() noexcept;
    Def* producer() noexcept;
    void setProducer(Def* def) noexcept;

  private:
    Inst* consumer_;
    Def* producer_;

    friend class Def;
};

using UseList = boost::intrusive::list<Use>;
using UseIterator = UseList::iterator;

class Def
{
  public:
    Def(OpCode opCode, int id, Type type) noexcept;

    Def(Def const&) = delete;
    Def(Def&&) = delete;
    Def& operator=(Def const&) = delete;
    Def& operator=(Def&&) = delete;

    OpCode opCode() const noexcept;

    Type type() const noexcept;

    int id() const noexcept;

    boost::iterator_range<UseIterator> uses() noexcept;

  protected:
    ~Def() noexcept;

    OpCode opCode_;

  protected:
    int id_;
    Type type_;

    UseList uses_;
    friend class Use;
};

void replace(Def& old, Def& replacement) noexcept;

class ScalarConstant final : public Def
{
  public:
    ScalarConstant(OpCode opCode, int id, Type type, std::uint64_t) noexcept;

    ScalarConstant(OpCode opCode, int id, Type type, double) noexcept;

    double floatValue() const noexcept;

    std::uint64_t integerValue() const noexcept;

  private:
    union
    {
        double floatValue_;
        std::uint64_t integerValue_;
    };
};

class Inst final : public Def, public boost::intrusive::list_base_hook<>
{
  public:
    Inst(OpCode opCode, int id, Type type, unsigned operandCount) noexcept;
    Inst(OpCode opCode, int id, Type type, InstFlags flags, unsigned operandCount) noexcept;
    ~Inst() noexcept;

    std::size_t operandCount() const noexcept;
    Def* getOperand(std::size_t index) noexcept;
    void setOperand(std::size_t index, Def* def) noexcept;

    void eraseOperand(unsigned index) noexcept;

    void identify(Def* def);

    InstFlags flags() const noexcept;

  private:
    InstFlags flags_;
    std::vector<Use> operands_;
};

using InstList = boost::intrusive::list<Inst>;
using InstIterator = InstList::iterator;

class BasicBlock
{
  public:
    BasicBlock(int id);
    ~BasicBlock() noexcept;

    int id() const noexcept;
    void setId(int v) noexcept;

    Inst& insertFront(std::unique_ptr<Inst> insn) noexcept;
    Inst& insertBack(std::unique_ptr<Inst> insn) noexcept;
    Inst& insertBefore(Inst& pos, std::unique_ptr<Inst> inst) noexcept;
    std::unique_ptr<Inst> erase(Inst& inst) noexcept;

    boost::iterator_range<InstIterator> instructions() noexcept;

    std::vector<BasicBlock*>& successors() noexcept;
    std::vector<BasicBlock*> const& predecessors() noexcept;

    std::size_t insertPredecessor(BasicBlock*);

  private:
    InstList instructions_;
    int id_;

    std::vector<BasicBlock *> successors_, predecessors_;
};

enum class ProgramType
{
    fragment,
    vertex,
    compute
};

class Program
{
  public:
    Program(ProgramType);
    ~Program() noexcept;

    ProgramType type() const noexcept;

    template <typename T, typename... Args>
    std::unique_ptr<T> createDef(OpCode opCode, Type type, Args&&... args);

    std::unique_ptr<BasicBlock> createBasicBlock();

    TypeContext& types() noexcept;

    ScalarConstant* getScalarConstant(Type type, std::uint64_t);

    ScalarConstant* getScalarConstant(Type type, double);

    unsigned defIdCount() const noexcept;

    BasicBlock& insertBack(std::unique_ptr<BasicBlock>);

    std::vector<std::unique_ptr<BasicBlock>>& basicBlocks() noexcept;

    boost::iterator_range<InstIterator> variables() noexcept;
    Inst& insertVariable(std::unique_ptr<Inst> inst) noexcept;
    std::unique_ptr<Inst> eraseVariable(Inst& inst) noexcept;

    Inst& appendParam(std::unique_ptr<Inst> param);

    std::vector<std::unique_ptr<Inst>>& params() noexcept;

    BasicBlock& initialBlock() noexcept;

  private:
    ProgramType type_;
    int nextDefIndex_;
    int nextBlockIndex_;
    TypeContext types_;

    std::vector<std::unique_ptr<ScalarConstant>> scalarConstants_;

    std::vector<std::unique_ptr<BasicBlock>> basicBlocks_;
    InstList variables_;
    std::vector<std::unique_ptr<Inst>> params_;
};

void print(std::ostream& os, Program& program);
}

void promoteVariables(hir::Program& program);
void splitComposites(hir::Program& program);
void eliminateDeadCode(hir::Program& program);
void lowerIO(hir::Program& program);
void orderBlocksRPO(hir::Program& program);

}
}

#endif
