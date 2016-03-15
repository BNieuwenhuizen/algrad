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


struct PhysReg
{
    unsigned reg;
};

using Temp_id = std::uint32_t;

class Arg final
{
  public:
    Arg() = default;
    explicit Arg(Temp_id r) noexcept;
    Arg(Temp_id r, PhysReg reg) noexcept;

    bool is_temp() const noexcept;
    Temp_id temp() const noexcept;
    void set_temp(Temp_id id) noexcept;

    bool isFixed() const noexcept;
    PhysReg physReg() const noexcept;
    void setFixed(PhysReg reg) noexcept;

    bool isConstant() const noexcept;
    std::uint32_t constantValue() const noexcept;

    void setKill(bool) noexcept;
    bool kill() const noexcept;

  private:
    union
    {
        Temp_id temp;
	std::uint32_t constant;
    } data_;
    std::uint32_t control_;

    using IsTempField = BitField<std::uint32_t, 16, 17, bool>;
    using KillField = BitField<std::uint32_t, 19, 20, bool>;
    using FixedField = BitField<std::uint32_t, 20, 21, bool>;
    using PhysRegField = BitField<std::uint32_t, 21, 32>;

    friend Arg integerConstant(std::uint32_t v) noexcept;
};

Arg integerConstant(std::uint32_t v) noexcept;

#define ALGRAD_COMPILER_LIR_OPCODES(_)                                                                                 \
    _(start)                                                                                                           \
    _(start_block)                                                                                                     \
    _(parallel_copy)                                                                                                   \
    _(phi)                                                                                                             \
    _(logical_branch)                                                                                                  \
    _(logical_cond_branch)                                                                                             \
    _(s_endpgm)                                                                                                        \
    _(v_cmp_lt_f32)                                                                                                    \
    _(exp)                                                                                                             \
    _(v_interp_p1_f32)                                                                                                 \
    _(v_interp_p2_f32)

enum class OpCode : std::uint16_t
{
#define HANDLE(v) v,
    ALGRAD_COMPILER_LIR_OPCODES(HANDLE)
#undef HANDLE
};

struct AuxiliaryVINTRPInfo
{
    unsigned attribute;
    unsigned channel;
};

struct AuxiliaryEXPInfo
{
    unsigned enable : 4;
    unsigned target : 6;
    bool compressed : 1;
    bool done : 1;
    bool validMask : 1;
};

union AuxiliaryInstInfo
{
    AuxiliaryVINTRPInfo vintrp;
    AuxiliaryEXPInfo exp;
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

    AuxiliaryInstInfo& aux() noexcept { return aux_; }
    AuxiliaryInstInfo const& aux() const noexcept { return aux_; }

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

    AuxiliaryInstInfo aux_;
};

class Block
{
  public:
    Block(int id) noexcept : id_{id} {}

    int id() const noexcept { return id_; }

    std::vector<std::unique_ptr<Inst>>& instructions() noexcept;

    std::vector<Block*>& logicalPredecessors() noexcept;
    std::vector<Block*>& logicalSuccessors() noexcept;
    std::vector<Block*>& linearizedPredecessors() noexcept;
    std::vector<Block*>& linearizedSuccessors() noexcept;

  private:
    int id_;
    std::vector<std::unique_ptr<Inst>> instructions_;
    std::vector<Block *> logicalPredecessors_, logicalSuccessors_;
    std::vector<Block *> linearizedPredecessors_, linearizedSuccessors_;
};

std::size_t findOrInsertBlock(std::vector<Block*>& arr, Block*);
std::size_t renameBlock(std::vector<Block*>& arr, Block* old, Block* replacement) noexcept;
std::size_t removeBlock(std::vector<Block*>& arr, Block*) noexcept;

struct Temp_info
{
    RegClass reg_class;
    unsigned size;
};

class Program
{
  public:
    Program();
    std::vector<std::unique_ptr<Block>>& blocks() noexcept;

    std::uint32_t allocate_temp(RegClass reg_class, unsigned size) noexcept;
    Temp_info const& temp_info(std::uint32_t index) const noexcept;
    std::uint32_t allocated_temp_count() const noexcept;

  private:
    std::vector<std::unique_ptr<Block>> blocks_;
    std::vector<Temp_info> temps_;
};

void print(std::ostream& os, Program& program);

inline Arg::Arg(Temp_id r) noexcept : data_{r}, control_{IsTempField::place(true)}
{
}

inline Arg::Arg(Temp_id r, PhysReg reg) noexcept
  : data_{r},
    control_{IsTempField::place(true) | FixedField::place(true) | PhysRegField::place(reg.reg)}
{
}

inline Arg
integerConstant(std::uint32_t v) noexcept
{
    Arg arg;
    arg.data_.constant = v;
    arg.control_ = Arg::IsTempField::place(false);
    return arg;
}

inline Arg
floatConstant(float v) noexcept
{
    std::uint32_t iv;
    std::memcpy(&iv, &v, 4);
    return integerConstant(iv);
}

inline bool
Arg::is_temp() const noexcept
{
    return IsTempField::extract(control_);
}

inline Temp_id
Arg::temp() const noexcept
{
    return data_.temp;
}

inline void
Arg::set_temp(Temp_id id) noexcept
{
    assert(is_temp());
    data_.temp = id;
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
    return !is_temp();
}

inline std::uint32_t
Arg::constantValue() const noexcept
{
    assert(isConstant());
    return data_.constant;
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
Program::allocate_temp(RegClass reg_class, unsigned size) noexcept
{
    auto id = temps_.size();
    temps_.push_back(Temp_info{reg_class, size});
    return id;
}

inline Temp_info const&
Program::temp_info(std::uint32_t index) const noexcept
{
    return temps_[index];
}

inline std::uint32_t
Program::allocated_temp_count() const noexcept
{
    return temps_.size();
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
