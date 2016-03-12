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

    friend class Arg;
};

struct PhysReg
{
    unsigned reg;
};

class Arg final
{
  public:
    Arg() = default;
    explicit Arg(Temp r) noexcept;
    Arg(Temp r, PhysReg reg) noexcept;
    explicit Arg(std::uint32_t v) noexcept;
    explicit Arg(float v) noexcept;

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

class Inst final
{
  public:
    Inst(OpCode opCode, std::size_t defCount, std::size_t opCount) noexcept;
    ~Inst() noexcept;

    OpCode opCode() const noexcept;

    std::size_t operandCount() const noexcept { return opCount_; }
    Arg& getOperand(std::size_t index) noexcept { return args()[defCount_ + index]; }

    std::size_t definitionCount() const noexcept { return defCount_; }
    Arg& getDefinition(std::size_t index) noexcept { return args()[index]; }

  private:
    Arg* args() noexcept;
    Arg const* args() const noexcept;

    OpCode opCode_;
    std::uint16_t defCount_;
    std::uint16_t opCount_;

    enum
    {
        internalArgCount_ = 4
    };
    union
    {
        Arg internalArgs_[4];
        Arg* externalArgs_;
    };
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

inline Arg::Arg(Temp r) noexcept : data_{r.id_}, control_{r.control_ | IsTempField::place(true)}
{
}

inline Arg::Arg(Temp r, PhysReg reg) noexcept
  : data_{r.id_},
    control_{r.control_ | IsTempField::place(true) | FixedField::place(true) | PhysRegField::place(reg.reg)}
{
}

inline Arg::Arg(std::uint32_t v) noexcept : data_{v}, control_{IsTempField::place(false)}
{
}

inline Arg::Arg(float v) noexcept : control_{IsTempField::place(false)}
{
    std::memcpy(&data_, &v, sizeof(float));
}

inline bool
Arg::isTemp() const noexcept
{
    return IsTempField::extract(control_);
}

inline Temp
Arg::getTemp() const noexcept
{
    return Temp{data_, control_ & (ClassField::mask | SizeField::mask)};
}

inline std::uint32_t
Arg::tempId() const noexcept
{
    assert(isTemp());
    return data_;
}

inline void
Arg::setTempId(std::uint32_t id) noexcept
{
    assert(isTemp());
    data_ = id;
}

inline RegClass
Arg::regClass() const noexcept
{
    assert(isTemp());
    return ClassField::extract(control_);
}

inline unsigned
Arg::size() const noexcept
{
    assert(isTemp());
    return SizeField::extract(control_);
}

inline bool
Arg::isFixed() const noexcept
{
    return FixedField::extract(control_);
}

inline PhysReg
Arg::physReg() const noexcept
{
    return {PhysRegField::extract(control_)};
}

inline void
Arg::setFixed(PhysReg reg) noexcept
{
    control_ = FixedField::insert(PhysRegField::insert(control_, reg.reg), true);
}

inline bool
Arg::isConstant() const noexcept
{
    return !isTemp();
}

inline std::uint32_t
Arg::constantValue() const noexcept
{
    assert(isConstant());
    return data_;
}

inline void
Arg::setKill(bool b) noexcept
{
    control_ = KillField::insert(control_, b);
}

inline bool
Arg::kill() const noexcept
{
    return KillField::extract(control_);
}

inline OpCode
Inst::opCode() const noexcept
{
    return opCode_;
}

inline Arg*
Inst::args() noexcept
{
    if (defCount_ + opCount_ > internalArgCount_)
        return externalArgs_;
    else
        return internalArgs_;
}

inline Arg const*
Inst::args() const noexcept
{
    if (defCount_ + opCount_ > internalArgCount_)
        return externalArgs_;
    else
        return internalArgs_;
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
void emit(lir::Program& program);
}
}
#endif
