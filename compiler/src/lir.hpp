#ifndef ALGRAD_LIR_HPP
#define ALGRAD_LIR_HPP

#include <boost/range/iterator_range.hpp>
#include <cstdint>
#include <cstring>
#include <iosfwd>
#include <memory>

namespace algrad {
namespace compiler {

namespace hir {
class Program;
}

namespace lir {
template <typename T, unsigned a, unsigned b, typename T2 = T>
struct BitField
{
    static constexpr T place(T2 v) noexcept { return (static_cast<T>(v) << a); }

    static constexpr T insert(T orig, T v) noexcept
    {
        return (orig & ~(((static_cast<T>(1) << (b - a)) - 1U) << a)) | (static_cast<T>(v) << a);
    }

    static constexpr T2 extract(T orig) noexcept
    {
        return static_cast<T2>((orig >> a) & ((static_cast<T>(1) << (b - a)) - 1U));
    }

    enum
    {
        mask = ((static_cast<T>(1) << (b - a)) - 1U) << a
    };
};

enum class RegClass
{
    sgpr,
    vgpr,
    scc
};

class Temp final
{
  public:
    Temp() = default;
    constexpr Temp(std::uint32_t id, RegClass cls, unsigned size) noexcept;
    constexpr Temp(std::uint32_t id, std::uint32_t control) noexcept;

    std::uint32_t id() const noexcept;
    RegClass regClass() const noexcept;
    unsigned size() const noexcept;

  private:
    std::uint32_t id_;
    std::uint32_t control_;

    using ClassField = BitField<std::uint32_t, 0, 2, RegClass>;
    using SizeField = BitField<std::uint32_t, 2, 7>;

    friend class Operand;
    friend class Def;
};

struct PhysReg
{
    unsigned reg;
};

class Operand final
{
  public:

    Operand() = default;
    explicit Operand(Temp r) noexcept;
    Operand(Temp r, PhysReg reg) noexcept;
    explicit Operand(std::uint32_t v) noexcept;
    explicit Operand(float v) noexcept;

    bool isTemp() const noexcept;
    Temp getTemp() const noexcept;

    std::uint32_t tempId() const noexcept;
    void setTempId(std::uint32_t) noexcept;
    RegClass regClass() const noexcept;
    unsigned size() const noexcept;

    bool isFixed() const noexcept;
    PhysReg physReg() const noexcept;
    void setFixed(PhysReg reg) noexcept;

    bool isConstant() const noexcept;
    std::uint32_t constantValue() const noexcept;

    void setKill(bool) noexcept;
    bool kill() const noexcept;

  private:
    std::uint32_t data_;
    std::uint32_t control_;

    using ClassField = BitField<std::uint32_t, 0, 2, RegClass>;
    using SizeField = BitField<std::uint32_t, 2, 7>;

    using IsTempField = BitField<std::uint32_t, 16, 17, bool>;
    using KillField = BitField<std::uint32_t, 21, 22, bool>;
    using FixedField = BitField<std::uint32_t, 22, 23, bool>;
    using PhysRegField = BitField<std::uint32_t, 23, 32>;
};

class Def final
{
  public:
    Def() = default;
    explicit Def(Temp r) noexcept;
    explicit Def(Temp t, PhysReg reg) noexcept;
    explicit Def(PhysReg reg) noexcept;

    bool isTemp() const noexcept;
    Temp getTemp() const noexcept;

    std::uint32_t tempId() const noexcept;
    void setTempId(std::uint32_t) noexcept;
    RegClass regClass() const noexcept;
    unsigned size() const noexcept;

    bool isFixed() const noexcept;
    PhysReg physReg() const noexcept;
    void setFixed(PhysReg reg) noexcept;

  private:
    std::uint32_t tempId_;
    std::uint32_t control_;

    using ClassField = BitField<std::uint32_t, 0, 2, RegClass>;
    using SizeField = BitField<std::uint32_t, 2, 7>;

    using IsTempField = BitField<std::uint32_t, 16, 17, bool>;

    using FixedField = BitField<std::uint32_t, 22, 23, bool>;
    using PhysRegField = BitField<std::uint32_t, 23, 32>;
};

#define ALGRAD_COMPILER_LIR_OPCODES(_)                                                                                 \
    _(start)                                                                                                           \
    _(parallel_copy)                                                                                                   \
    _(phi)                                                                                                             \
    _(s_endpgm)                                                                                                        \
    _(exp)                                                                                                             \
    _(v_interp_p1_f32)                                                                                                 \
    _(v_interp_p2_f32)

enum class OpCode : std::uint16_t
{
#define HANDLE(v) v,
    ALGRAD_COMPILER_LIR_OPCODES(HANDLE)
#undef HANDLE
};

class Inst
{
  public:
    Inst(OpCode opCode) noexcept : opCode_{opCode} {}
    virtual ~Inst() noexcept {}

    boost::iterator_range<Operand*> args() noexcept;
    boost::iterator_range<Operand const*> args() const noexcept;

    OpCode opCode() const noexcept;

    virtual std::size_t operandCount() const noexcept { return 0; }
    virtual Operand& getOperand(std::size_t index) noexcept { __builtin_unreachable(); }

    virtual std::size_t definitionCount() const noexcept { return 0; }
    virtual Def& getDefinition(std::size_t index) noexcept { __builtin_unreachable(); }

  protected:
    Inst(Inst&&) = default;
    Inst(Inst const&) = default;
    Inst& operator=(Inst&&) = default;
    Inst& operator=(Inst const&) = default;

  private:
    OpCode opCode_;
};

template <std::size_t Op, std::size_t DefCount>
class FixedInst : public Inst
{
  public:
    FixedInst(OpCode opCode) noexcept : Inst{opCode} {}

    std::size_t operandCount() const noexcept final override { return Op; }
    Operand& getOperand(std::size_t index) noexcept final override { return operands_[index]; }
    std::size_t definitionCount() const noexcept final override { return DefCount; }
    Def& getDefinition(std::size_t index) noexcept final override { return defs_[index]; }

  private:
    Def defs_[DefCount];
    Operand operands_[Op];
};

class StartInst final : public Inst
{
  public:
    StartInst(std::size_t genCount)
      : Inst{OpCode::start}
      , defs_(genCount)
    {
    }

    std::size_t definitionCount() const noexcept override { return defs_.size(); }
    Def& getDefinition(std::size_t index) noexcept override { return defs_[index]; }

  private:
    std::vector<Def> defs_;
};

class ParallelCopy final : public Inst
{
  public:
    ParallelCopy(std::size_t count) noexcept : Inst{OpCode::parallel_copy}, defs_(count), operands_(count) {}

    std::size_t operandCount() const noexcept final override { return operands_.size(); }
    Operand& getOperand(std::size_t index) noexcept final override { return operands_[index]; }
    std::size_t definitionCount() const noexcept final override { return defs_.size(); }
    Def& getDefinition(std::size_t index) noexcept final override { return defs_[index]; }
  private:
    std::vector<Def> defs_;
    std::vector<Operand> operands_;
};

class PhiInstruction final : public Inst
{
  public:
    PhiInstruction(std::size_t operands)
      : Inst{OpCode::phi}
      , operands_(operands)
    {
    }

    std::size_t operandCount() const noexcept final override { return operands_.size(); }
    Operand& getOperand(std::size_t index) noexcept final override { return operands_[index]; }
    std::size_t definitionCount() const noexcept final override { return 1; }
    Def& getDefinition(std::size_t index) noexcept final override { return def_; }

    void insertOperand(std::size_t index, Operand op) { operands_.insert(operands_.begin() + index, op); }

    void eraseOperand(std::size_t index) noexcept { operands_.erase(operands_.begin() + index); }
  private:
    Def def_;
    std::vector<Operand> operands_;
};

class VInterpP1F32Inst final : public FixedInst<2, 1>
{
  public:
    VInterpP1F32Inst(unsigned attribute, unsigned component) noexcept : FixedInst{OpCode::v_interp_p1_f32},
                                                                        attribute_{attribute},
                                                                        component_{component}
    {
    }

  private:
    unsigned attribute_;
    unsigned component_;
};

class VInterpP2F32Inst final : public FixedInst<3, 1>
{
  public:
    VInterpP2F32Inst(unsigned attribute, unsigned component) noexcept : FixedInst{OpCode::v_interp_p2_f32},
                                                                        attribute_{attribute},
                                                                        component_{component}
    {
    }

  private:
    unsigned attribute_;
    unsigned component_;
};

class ExportInst final : public FixedInst<4, 0>
{
  public:
    ExportInst(unsigned enabledMask, unsigned dest, bool compressed, bool done, bool validMask) noexcept
      : FixedInst{OpCode::exp},
        enabledMask_{enabledMask},
        dest_{dest},
        compressed_{compressed},
        done_{done},
        validMask_{validMask}
    {
    }

  private:
    unsigned enabledMask_;
    unsigned dest_;
    bool compressed_;
    bool done_;
    bool validMask_;
};
class EndProgramInstruction final : public Inst
{
  public:
    EndProgramInstruction() noexcept : Inst{OpCode::s_endpgm} {}
};

class Block
{
  public:
    std::vector<std::unique_ptr<Inst>>& instructions() noexcept;

    std::vector<Block*>& logicalPredecessors() noexcept;
    std::vector<Block*>& logicalSuccessors() noexcept;
    std::vector<Block*>& linearizedPredecessors() noexcept;
    std::vector<Block*>& linearizedSuccessors() noexcept;

  private:
    std::vector<std::unique_ptr<Inst>> instructions_;
    std::vector<Block *> logicalPredecessors_, logicalSuccessors_;
    std::vector<Block *> linearizedPredecessors_, linearizedSuccessors_;
};

std::size_t findOrInsertBlock(std::vector<Block*>& arr, Block*);
std::size_t renameBlock(std::vector<Block*>& arr, Block* old, Block* replacement) noexcept;
std::size_t removeBlock(std::vector<Block*>& arr, Block*) noexcept;

class Program
{
  public:
    Program();
    std::vector<std::unique_ptr<Block>>& blocks() noexcept;

    std::uint32_t allocateId() noexcept;
    std::uint32_t allocatedIds() const noexcept;

  private:
    std::vector<std::unique_ptr<Block>> blocks_;
    std::uint32_t nextId_;
};

void print(std::ostream& os, Program& program);

constexpr Temp::Temp(std::uint32_t id, RegClass cls, unsigned size) noexcept
  : id_{id},
    control_{ClassField::place(cls) | SizeField::place(size)}
{
}

constexpr Temp::Temp(std::uint32_t id, std::uint32_t control) noexcept : id_{id}, control_{control}
{
}

inline std::uint32_t
Temp::id() const noexcept
{
    return id_;
}

inline RegClass
Temp::regClass() const noexcept
{
    return ClassField::extract(control_);
}

inline unsigned
Temp::size() const noexcept
{
    return SizeField::extract(control_);
}

inline Operand::Operand(Temp r) noexcept : data_{r.id_}, control_{r.control_ | IsTempField::place(true)}
{
}

inline Operand::Operand(Temp r, PhysReg reg) noexcept
  : data_{r.id_},
    control_{r.control_ | IsTempField::place(true) | FixedField::place(true) | PhysRegField::place(reg.reg)}
{
}

inline Operand::Operand(std::uint32_t v) noexcept : data_{v}, control_{IsTempField::place(false)}
{
}

inline Operand::Operand(float v) noexcept : control_{IsTempField::place(false)}
{
    std::memcpy(&data_, &v, sizeof(float));
}

inline bool
Operand::isTemp() const noexcept
{
    return IsTempField::extract(control_);
}

inline Temp
Operand::getTemp() const noexcept
{
    return Temp{data_, control_ & (ClassField::mask | SizeField::mask)};
}

inline std::uint32_t
Operand::tempId() const noexcept
{
    assert(isTemp());
    return data_;
}

inline void
Operand::setTempId(std::uint32_t id) noexcept
{
    assert(isTemp());
    data_ = id;
}

inline RegClass
Operand::regClass() const noexcept
{
    assert(isTemp());
    return ClassField::extract(control_);
}

inline unsigned
Operand::size() const noexcept
{
    assert(isTemp());
    return SizeField::extract(control_);
}

inline bool
Operand::isFixed() const noexcept
{
    return FixedField::extract(control_);
}

inline PhysReg
Operand::physReg() const noexcept
{
    return {PhysRegField::extract(control_)};
}

inline void
Operand::setFixed(PhysReg reg) noexcept
{
    control_ = FixedField::insert(PhysRegField::insert(control_, reg.reg), true);
}

inline bool
Operand::isConstant() const noexcept
{
    return !isTemp();
}

inline std::uint32_t
Operand::constantValue() const noexcept
{
    assert(isConstant());
    return data_;
}

inline void
Operand::setKill(bool b) noexcept
{
    control_ = KillField::insert(control_, b);
}

inline bool
Operand::kill() const noexcept
{
    return KillField::extract(control_);
}

inline Def::Def(Temp r) noexcept : tempId_{r.id_}, control_{r.control_ | IsTempField::place(true)}
{
}

inline Def::Def(Temp t, PhysReg reg) noexcept
  : tempId_{t.id_},
    control_{t.control_ | IsTempField::place(true) | FixedField::place(true) | PhysRegField::place(reg.reg)}
{
}

inline Def::Def(PhysReg reg) noexcept : tempId_{0}, control_{FixedField::place(true) | PhysRegField::place(reg.reg)}
{
}

inline bool
Def::isTemp() const noexcept
{
    return IsTempField::extract(control_);
}

inline Temp
Def::getTemp() const noexcept
{
    return Temp{tempId_, control_ & (ClassField::mask | SizeField::mask)};
}

inline std::uint32_t
Def::tempId() const noexcept
{
    assert(isTemp());
    return tempId_;
}

inline void
Def::setTempId(std::uint32_t id) noexcept
{
    assert(isTemp());
    tempId_ = id;
}

inline RegClass
Def::regClass() const noexcept
{
    assert(isTemp());
    return ClassField::extract(control_);
}

inline unsigned
Def::size() const noexcept
{
    assert(isTemp());
    return SizeField::extract(control_);
}

inline bool
Def::isFixed() const noexcept
{
    return FixedField::extract(control_);
}

inline PhysReg
Def::physReg() const noexcept
{
    return {PhysRegField::extract(control_)};
}

inline void
Def::setFixed(PhysReg reg) noexcept
{
    control_ = FixedField::insert(PhysRegField::insert(control_, reg.reg), true);
}

inline OpCode
Inst::opCode() const noexcept
{
    return opCode_;
}

inline std::vector<std::unique_ptr<Inst>>&
Block::instructions() noexcept
{
    return instructions_;
}

inline std::vector<Block*>&
Block::logicalPredecessors() noexcept
{
    return logicalPredecessors_;
}

inline std::vector<Block*>&
Block::logicalSuccessors() noexcept
{
    return logicalSuccessors_;
}

inline std::vector<Block*>&
Block::linearizedPredecessors() noexcept
{
    return linearizedPredecessors_;
}

inline std::vector<Block*>&
Block::linearizedSuccessors() noexcept
{
    return linearizedSuccessors_;
}

inline std::size_t
findOrInsertBlock(std::vector<Block*>& arr, Block* n)
{
    for (std::size_t i = 0; i < arr.size(); ++i) {
        if (arr[i] == n) {
            return i;
        }
    }
    auto ret = arr.size();
    arr.push_back(n);
    return ret;
}

inline std::size_t
renameBlock(std::vector<Block*>& arr, Block* old, Block* replacement) noexcept
{
    for (std::size_t i = 0; i < arr.size(); ++i) {
        if (arr[i] == old) {
            arr[i] = replacement;
            return i;
        }
    }
    std::terminate();
}

inline std::size_t
removeBlock(std::vector<Block*>& arr, Block* b) noexcept
{
    for (std::size_t i = 0; i < arr.size(); ++i) {
        if (arr[i] == b) {
            arr.erase(arr.begin() + i);
            return i;
        }
    }
    std::terminate();
}

inline std::uint32_t
Program::allocateId() noexcept
{
    return nextId_++;
}

inline std::uint32_t
Program::allocatedIds() const noexcept
{
    return nextId_;
}

inline std::vector<std::unique_ptr<Block>>&
Program::blocks() noexcept
{
    return blocks_;
}
}

std::unique_ptr<lir::Program> selectInstructions(hir::Program& program);
void allocateRegisters(lir::Program& program);
}
}
#endif
