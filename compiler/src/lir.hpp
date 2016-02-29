#ifndef ALGRAD_LIR_HPP
#define ALGRAD_LIR_HPP

#include <boost/range/iterator_range.hpp>
#include <cstdint>
#include <cstring>
#include <iosfwd>
#include <memory>

namespace algrad {
namespace compiler {

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

class LReg
{
  public:
    LReg() = default;
    constexpr LReg(std::uint32_t id, RegClass cls, unsigned size) noexcept;

  private:
    std::uint32_t id_;
    std::uint32_t control_;

    using ClassField = BitField<std::uint32_t, 0, 2, RegClass>;
    using SizeField = BitField<std::uint32_t, 2, 7>;

    friend class LArg;
};

struct PhysReg
{
    unsigned reg;
};

class LArg
{
  public:
    enum class Role
    {
        constant,
        use,
        def
    };

    LArg() = default;
    LArg(LReg r, Role role) noexcept;
    LArg(LReg r, Role role, PhysReg reg) noexcept;

    static inline LArg int32Constant(std::uint32_t v) noexcept;
    static inline LArg floatConstant(float v) noexcept;

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

enum class LOpCode
{
#define HANDLE(v) v,
    ALGRAD_COMPILER_LIR_OPCODES(HANDLE)
#undef HANDLE
};

class LInst final
{
  public:
    LInst(LOpCode opCode, unsigned argCount);
    LInst(LInst&&) noexcept;
    LInst(LInst const&);
    LInst& operator=(LInst&&) noexcept;
    LInst& operator=(LInst const&);
    ~LInst() noexcept;

    boost::iterator_range<LArg*> args() noexcept;
    boost::iterator_range<LArg const*> args() const noexcept;

    LOpCode opCode() const noexcept;

  private:
    LOpCode opCode_;
    std::uint16_t argCount_;
    enum
    {
        internalArgCount = 3
    };
    union
    {
        LArg* externalArgs_;
        LArg internalArgs_[internalArgCount];
    };
};

class LBlock
{
  public:
    std::vector<LInst>& instructions() noexcept;
    std::vector<LInst> const& instructions() const noexcept;

  private:
    std::vector<LInst> instructions_;
};

class LFunction
{
  public:
    std::vector<std::unique_ptr<LBlock>>& blocks() noexcept;

  private:
    std::vector<std::unique_ptr<LBlock>> blocks_;
};

class LProgram
{
  public:
    LProgram();
    std::vector<std::unique_ptr<LFunction>>& functions() noexcept;

    std::uint32_t allocateId() noexcept;
    std::uint32_t allocatedIds() const noexcept;

  private:
    std::vector<std::unique_ptr<LFunction>> functions_;
    std::uint32_t nextId_;
};

void print(std::ostream& os, LProgram& program);

namespace hir {
class Program;
}

std::unique_ptr<LProgram> selectInstructions(hir::Program& program);
void allocateRegisters(LProgram& program);

constexpr LReg::LReg(std::uint32_t id, RegClass cls, unsigned size) noexcept
  : id_{id},
    control_{ClassField::place(cls) | SizeField::place(size)}
{
}

inline LArg::LArg(LReg r, Role role) noexcept : data_{r.id_}, control_{r.control_ | RoleField::place(role)}
{
}

inline LArg::LArg(LReg r, Role role, PhysReg reg) noexcept
  : data_{r.id_},
    control_{r.control_ | RoleField::place(role) | FixedField::place(true) | PhysRegField::place(reg.reg)}
{
}
inline LArg
LArg::int32Constant(std::uint32_t v) noexcept
{
    LArg ret;
    ret.data_ = v;
    ret.control_ = RoleField::place(Role::constant);
    return ret;
}

inline LArg
LArg::floatConstant(float v) noexcept
{
    LArg ret;
    std::memcpy(&ret.data_, &v, sizeof(float));
    ret.control_ = RoleField::place(Role::constant);
    return ret;
}

inline LArg::Role
LArg::role() const noexcept
{
    return RoleField::extract(control_);
}

inline RegClass
LArg::regClass() const noexcept
{
    return ClassField::extract(control_);
}

inline unsigned
LArg::size() const noexcept
{
    return SizeField::extract(control_);
}

inline std::uint32_t
LArg::data() const noexcept
{
    return data_;
}

inline void
LArg::data(std::uint32_t v) noexcept
{
    data_ = v;
}

inline bool
LArg::isFixed() const noexcept
{
    return FixedField::extract(control_);
}

inline PhysReg
LArg::physReg() const noexcept
{
    return {PhysRegField::extract(control_)};
}

inline void
LArg::setFixed(PhysReg reg) noexcept
{
    control_ = FixedField::insert(PhysRegField::insert(control_, reg.reg), true);
}

inline void
LArg::setKill(bool b) noexcept
{
    control_ = KillField::insert(control_, b);
}

inline bool
LArg::kill() const noexcept
{
    return KillField::extract(control_);
}

inline boost::iterator_range<LArg*>
LInst::args() noexcept
{
    LArg* base = argCount_ > internalArgCount ? externalArgs_ : internalArgs_;
    return {base, base + argCount_};
}

inline boost::iterator_range<LArg const*>
LInst::args() const noexcept
{
    LArg const* base = argCount_ > internalArgCount ? externalArgs_ : internalArgs_;
    return {base, base + argCount_};
}

inline LOpCode
LInst::opCode() const noexcept
{
    return opCode_;
}

inline std::vector<LInst>&
LBlock::instructions() noexcept
{
    return instructions_;
}

inline std::vector<LInst> const&
LBlock::instructions() const noexcept
{
    return instructions_;
}

inline std::vector<std::unique_ptr<LBlock>>&
LFunction::blocks() noexcept
{
    return blocks_;
}

inline std::uint32_t
LProgram::allocateId() noexcept
{
    return nextId_++;
}

inline std::uint32_t
LProgram::allocatedIds() const noexcept
{
    return nextId_;
}

inline std::vector<std::unique_ptr<LFunction>>&
LProgram::functions() noexcept
{
    return functions_;
}
}
}
#endif
