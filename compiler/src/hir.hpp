#ifndef ALGRAD_COMPILER_IR_HPP
#define ALGRAD_COMPILER_IR_HPP

#include <cstdint>
#include <memory>

#include "types.hpp"

#include <boost/range/iterator_range.hpp>
#include <iosfwd>

namespace algrad {
namespace compiler {

namespace hir {

#define ALGRAD_COMPILER_HIR_OPCODES(_)                                                                                 \
    _(function, InstFlags::none)                                                                                       \
    _(constant, InstFlags::none)                                                                                       \
    _(parameter, InstFlags::none)                                                                                      \
    _(identity, InstFlags::none)                                                                                       \
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

class Def
{
  public:
    Def(OpCode opCode, int id, Type type) noexcept;

    OpCode opCode() const noexcept;

    Type type() const noexcept;

    int id() const noexcept;

  protected:
    Def(Def const&) noexcept = default;

    ~Def() noexcept;

    OpCode opCode_;

  protected:
    std::uint16_t operandCount_;
    int id_;
    Type type_;
};

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

class Inst final : public Def
{
  public:
    Inst(OpCode opCode, int id, Type type, unsigned operandCount) noexcept;
    Inst(OpCode opCode, int id, Type type, InstFlags flags, unsigned operandCount) noexcept;

    Inst(Inst const&);

    Inst& operator=(Inst&&);

    ~Inst() noexcept;

    std::size_t operandCount() const noexcept;
    Def* getOperand(std::size_t index) const noexcept;
    void setOperand(std::size_t index, Def* def) noexcept;

    void eraseOperand(unsigned index) noexcept;

    void identify(Def* def);

    InstFlags flags() const noexcept;

  private:
    InstFlags flags_;

    enum
    {
        internalOperandCount = 3
    };
    union
    {
        Def* internalOperands_[internalOperandCount];
        Def** externalOperands_;
    };
};

class BasicBlock
{
  public:
    BasicBlock(int id);

    int id() const noexcept;

    Inst& insertBack(std::unique_ptr<Inst> insn);

    std::vector<std::unique_ptr<Inst>>& instructions() noexcept;

    std::vector<BasicBlock*>& successors() noexcept;
    std::vector<BasicBlock*> const& predecessors() noexcept;

    std::size_t insertPredecessor(BasicBlock*);

  private:
    std::vector<std::unique_ptr<Inst>> instructions_;
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

    std::vector<std::unique_ptr<BasicBlock>> const& basicBlocks() noexcept;

    std::vector<std::unique_ptr<Inst>>& variables() noexcept;

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
    std::vector<std::unique_ptr<Inst>> variables_;
    std::vector<std::unique_ptr<Inst>> params_;
};

void print(std::ostream& os, Program& program);
}

void promoteVariables(hir::Program& program);

void splitComposites(hir::Program& program);

void eliminateDeadCode(hir::Program& program);

void lowerIO(hir::Program& program);
}
}

#endif
