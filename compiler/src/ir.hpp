#ifndef ALGRAD_COMPILER_IR_HPP
#define ALGRAD_COMPILER_IR_HPP

#include <cstdint>
#include <memory>

#include "types.hpp"

#include <boost/range/iterator_range.hpp>
#include <iosfwd>

namespace algrad {
namespace compiler {

#define ALGRAD_COMPILER_OPCODES(_)                                                                                     \
    _(function)                                                                                                        \
    _(constant)                                                                                                        \
    _(parameter)                                                                                                       \
    _(identity)                                                                                                        \
    _(variable)                                                                                                        \
    _(ret)                                                                                                             \
    _(accessChain)                                                                                                     \
    _(load)                                                                                                            \
    _(store)                                                                                                           \
    _(compositeConstruct)                                                                                              \
    _(compositeExtract)                                                                                                \
    _(gcnInterpolate)                                                                                                  \
    _(gcnExport)

enum class OpCode : std::uint16_t
{
#define HANDLE(v) v,
    ALGRAD_COMPILER_OPCODES(HANDLE)
#undef HANDLE
};

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

class Instruction final : public Def
{
  public:
    Instruction(OpCode opCode, int id, Type type, unsigned operandCount) noexcept;
    Instruction(Instruction const&);
    Instruction& operator=(Instruction&&);
    ~Instruction() noexcept;

    boost::iterator_range<Def**> operands() noexcept;
    void eraseOperand(unsigned index) noexcept;

    void identify(Def* def);

  private:
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

    Instruction& insertBack(std::unique_ptr<Instruction> insn);

    std::vector<std::unique_ptr<Instruction>>& instructions() noexcept;

  private:
    std::vector<std::unique_ptr<Instruction>> instructions_;
    int id_;
};

class Function : public Def
{
  public:
    Function(int id, Type type);
    ~Function() noexcept;

    BasicBlock& insertBack(std::unique_ptr<BasicBlock>);
    BasicBlock& insertFront(std::unique_ptr<BasicBlock>);
    std::unique_ptr<BasicBlock> erase(BasicBlock&);

    std::vector<std::unique_ptr<BasicBlock>> const& basicBlocks() noexcept;
    std::vector<std::unique_ptr<Instruction>>& variables() noexcept;

    Instruction& appendParam(std::unique_ptr<Instruction> param);
    std::vector<std::unique_ptr<Instruction>>& params() noexcept;

    BasicBlock& initialBlock() noexcept;

  private:
    std::vector<std::unique_ptr<BasicBlock>> basicBlocks_;
    std::vector<std::unique_ptr<Instruction>> variables_;
    std::vector<std::unique_ptr<Instruction>> params_;
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

    Function* createFunction(Type type);

    TypeContext& types() noexcept;

    std::vector<std::unique_ptr<Function>>& functions() noexcept;

    ScalarConstant* getScalarConstant(Type type, std::uint64_t);
    ScalarConstant* getScalarConstant(Type type, double);

    void mainFunction(Function&) noexcept;
    Function& mainFunction() noexcept;

    unsigned defIdCount() const noexcept;

  private:
    ProgramType type_;
    int nextDefIndex_;
    int nextBlockIndex_;
    TypeContext types_;

    std::vector<std::unique_ptr<Function>> functions_;
    Function* mainFunction_;
    std::vector<std::unique_ptr<ScalarConstant>> scalarConstants_;
};

void print(std::ostream& os, Program& program);

void promoteVariables(Program& program);
void splitComposites(Program& program);
void eliminateDeadCode(Program& program);
void lowerIO(Program& program);

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

inline boost::iterator_range<Def**>
Instruction::operands() noexcept
{
    if (operandCount_ <= internalOperandCount)
        return {internalOperands_, internalOperands_ + operandCount_};
    else
        return {externalOperands_, externalOperands_ + operandCount_};
}

inline int
BasicBlock::id() const noexcept
{
    return id_;
}

inline std::vector<std::unique_ptr<Instruction>>&
BasicBlock::instructions() noexcept
{
    return instructions_;
}

inline std::vector<std::unique_ptr<BasicBlock>> const&
Function::basicBlocks() noexcept
{
    return basicBlocks_;
}

inline std::vector<std::unique_ptr<Instruction>>&
Function::variables() noexcept
{
    return variables_;
}

inline std::vector<std::unique_ptr<Instruction>>&
Function::params() noexcept
{
    return params_;
}

inline BasicBlock&
Function::initialBlock() noexcept
{
    return *basicBlocks_.front();
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

inline std::vector<std::unique_ptr<Function>>&
Program::functions() noexcept
{
    return functions_;
}

inline void
Program::mainFunction(Function& f) noexcept
{
    mainFunction_ = &f;
}
inline Function&
Program::mainFunction() noexcept
{
    return *mainFunction_;
}

inline unsigned
Program::defIdCount() const noexcept
{
    return nextDefIndex_;
}
}
}

#endif
