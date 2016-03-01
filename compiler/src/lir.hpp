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
};

enum class RegClass
{
    sgpr,
    vgpr
};

class Reg
{
  public:
    Reg() = default;
    constexpr Reg(std::uint32_t id, RegClass cls, unsigned size) noexcept;

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

class Arg
{
  public:
    enum class Role
    {
        constant,
        use,
        def
    };

    Arg() = default;
    Arg(Reg r, Role role) noexcept;
    Arg(Reg r, Role role, PhysReg reg) noexcept;

    static inline Arg int32Constant(std::uint32_t v) noexcept;
    static inline Arg floatConstant(float v) noexcept;

    Role role() const noexcept;
    RegClass regClass() const noexcept;
    unsigned size() const noexcept;

    std::uint32_t data() const noexcept;
    void data(std::uint32_t) noexcept;

    bool isFixed() const noexcept;
    PhysReg physReg() const noexcept;

    void setFixed(PhysReg reg) noexcept;

    void setKill(bool) noexcept;
    bool kill() const noexcept;

  private:
    std::uint32_t data_;
    std::uint32_t control_;

    using ClassField = BitField<std::uint32_t, 0, 2, RegClass>;
    using SizeField = BitField<std::uint32_t, 2, 7>;

    using RoleField = BitField<std::uint32_t, 16, 19, Role>;
    using KillField = BitField<std::uint32_t, 21, 22, bool>;
    using FixedField = BitField<std::uint32_t, 22, 23, bool>;
    using PhysRegField = BitField<std::uint32_t, 23, 32>;
};

#define ALGRAD_COMPILER_LIR_OPCODES(_)                                                                                 \
    _(start)                                                                                                           \
    _(parallel_copy)                                                                                                   \
    _(s_endpgm)                                                                                                        \
    _(exp)                                                                                                             \
    _(v_interp_p1_f32)                                                                                                 \
    _(v_interp_p2_f32)

enum class OpCode
{
#define HANDLE(v) v,
    ALGRAD_COMPILER_LIR_OPCODES(HANDLE)
#undef HANDLE
};

class Inst final
{
  public:
    Inst(OpCode opCode, unsigned argCount);
    Inst(Inst&&) noexcept;
    Inst(Inst const&);
    Inst& operator=(Inst&&) noexcept;
    Inst& operator=(Inst const&);
    ~Inst() noexcept;

    boost::iterator_range<Arg*> args() noexcept;
    boost::iterator_range<Arg const*> args() const noexcept;

    OpCode opCode() const noexcept;

  private:
    OpCode opCode_;
    std::uint16_t argCount_;
    enum
    {
        internalArgCount = 3
    };
    union
    {
        Arg* externalArgs_;
        Arg internalArgs_[internalArgCount];
    };
};

class Block
{
  public:
    std::vector<Inst>& instructions() noexcept;
    std::vector<Inst> const& instructions() const noexcept;

  private:
    std::vector<Inst> instructions_;
};

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


constexpr Reg::Reg(std::uint32_t id, RegClass cls, unsigned size) noexcept
  : id_{id},
    control_{ClassField::place(cls) | SizeField::place(size)}
{
}

inline Arg::Arg(Reg r, Role role) noexcept : data_{r.id_}, control_{r.control_ | RoleField::place(role)}
{
}

inline Arg::Arg(Reg r, Role role, PhysReg reg) noexcept
  : data_{r.id_},
    control_{r.control_ | RoleField::place(role) | FixedField::place(true) | PhysRegField::place(reg.reg)}
{
}
inline Arg
Arg::int32Constant(std::uint32_t v) noexcept
{
    Arg ret;
    ret.data_ = v;
    ret.control_ = RoleField::place(Role::constant);
    return ret;
}

inline Arg
Arg::floatConstant(float v) noexcept
{
    Arg ret;
    std::memcpy(&ret.data_, &v, sizeof(float));
    ret.control_ = RoleField::place(Role::constant);
    return ret;
}

inline Arg::Role
Arg::role() const noexcept
{
    return RoleField::extract(control_);
}

inline RegClass
Arg::regClass() const noexcept
{
    return ClassField::extract(control_);
}

inline unsigned
Arg::size() const noexcept
{
    return SizeField::extract(control_);
}

inline std::uint32_t
Arg::data() const noexcept
{
    return data_;
}

inline void
Arg::data(std::uint32_t v) noexcept
{
    data_ = v;
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

inline boost::iterator_range<Arg*>
Inst::args() noexcept
{
    Arg* base = argCount_ > internalArgCount ? externalArgs_ : internalArgs_;
    return {base, base + argCount_};
}

inline boost::iterator_range<Arg const*>
Inst::args() const noexcept
{
    Arg const* base = argCount_ > internalArgCount ? externalArgs_ : internalArgs_;
    return {base, base + argCount_};
}

inline OpCode
Inst::opCode() const noexcept
{
    return opCode_;
}

inline std::vector<Inst>&
Block::instructions() noexcept
{
    return instructions_;
}

inline std::vector<Inst> const&
Block::instructions() const noexcept
{
    return instructions_;
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
