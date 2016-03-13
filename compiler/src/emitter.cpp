#include "lir.hpp"

#include <fstream>
#include <unordered_map>

namespace algrad {
namespace compiler {

namespace {
class Label
{
  public:
    Label() noexcept : visited_{false} {}
  private:
    std::vector<std::uint32_t> references_;
    std::uint32_t index;
    bool visited_;

    friend class Emitter;
};

unsigned
getVGPR(lir::Arg arg)
{
    assert(arg.isTemp() && arg.isFixed());
    auto v = arg.physReg();

    assert(v.reg >= 256);
    return v.reg - 256u;
}

unsigned
getSGPR(lir::Arg arg)
{
    assert(arg.isTemp() && arg.isFixed());
    auto v = arg.physReg();

    assert(v.reg < 128);
    return v.reg;
}

unsigned
getSSRC(lir::Arg arg, bool& extended, std::uint32_t& extension)
{
    if (arg.isTemp()) {
        assert(arg.isFixed());
        auto v = arg.physReg();

        assert(v.reg < 128);
        return v.reg;
    } else {
        assert(!extended);
        extended = true;
        extension = arg.constantValue();
        return 255U;
    }
}

unsigned
getVSRC(lir::Arg arg, bool& extended, std::uint32_t& extension)
{
    if (arg.isTemp()) {
        assert(arg.isFixed());
        auto v = arg.physReg();

        return v.reg;
    } else {
        assert(!extended);
        extended = true;
        extension = arg.constantValue();
        return 255U;
    }
}

enum class SOP2OpCode
{
    s_add_u32 = 0,
    s_and_b32 = 12,
    s_and_b64 = 13,
    s_or_b32 = 14,
    s_or_b64 = 15,
    s_andn2_b32 = 18,
    s_andn2_b64 = 19
};

enum class SOP1OpCode
{
    s_mov_b32 = 0,
    s_mov_b64 = 1
};

enum class SOPPOpCode
{
    s_nop = 0,
    s_endpgm = 1
};

enum class VOP2OpCode
{
    v_cndmask_b32 = 0,
    v_add_f32 = 1,
    v_sub_f32 = 2
};

enum class VOP1OpCode
{
    v_nop = 0,
    v_mov_b32 = 1,
};

enum class VOPCOpCode
{
    v_cmp_lt_f32 = 0x41
};

enum class VINTRPOpCode
{
    v_interp_p1_f32 = 0,
    v_interp_p2_f32 = 1,
    v_interp_mov_f32 = 2
};
class Emitter
{
  public:
    Emitter() {}

    void startBlock(lir::Block& block)
    {
        auto& label = blockLabels_[&block];
        label.visited_ = true;
        label.index = data_.size();
        for (auto ref : label.references_) {
            unsigned v = label.index - ref - 1;
            data_[ref] = (data_[ref] & 0xFFFF0000U) | (v & 0xFFFF);
        }
        label.references_.clear();
    }

    void encodeSOP2(SOP2OpCode opCode, lir::Arg dest, lir::Arg src1, lir::Arg src2)
    {
        bool extended = false;
        std::uint32_t extension;
        data_.push_back((0b10U << 30) | (static_cast<unsigned>(opCode) << 23) | (getSGPR(dest) << 16) |
                        (getSSRC(src2, extended, extension) << 8) | getSSRC(src1, extended, extension));
        if (extended)
            data_.push_back(extension);
    }

    void encodeSOP1(SOP1OpCode opCode, lir::Arg dest, lir::Arg src)
    {
        bool extended = false;
        std::uint32_t extension;
        data_.push_back((0b101111101U << 23) | (getSGPR(dest) << 16) | (static_cast<unsigned>(opCode) << 8) |
                        getSSRC(src, extended, extension));
        if (extended)
            data_.push_back(extension);
    }

    void encodeSOPP(SOPPOpCode opCode, lir::Block& block)
    {
        auto& label = blockLabels_[&block];
        if (!label.visited_)
            label.references_.push_back(data_.size());
        encodeSOPP(opCode, label.index - data_.size() - 1);
    }

    void encodeSOPP(SOPPOpCode opCode, unsigned imm)
    {
        data_.push_back((0b101111111U << 23) | (static_cast<unsigned>(opCode) << 16) | (imm & 0xFFFFU));
    }

    void encodeVOP2(VOP2OpCode opCode, lir::Arg dest, lir::Arg src1, lir::Arg src2)
    {
        bool extended = false;
        std::uint32_t extension;
        data_.push_back((0b0U << 31) | (static_cast<unsigned>(opCode) << 25) | (getVGPR(dest) << 17) |
                        (getVGPR(src2) << 9) | getVSRC(src1, extended, extension));
        if (extended)
            data_.push_back(extension);
    }

    void encodeVOPC(VOPCOpCode opCode, lir::Arg src1, lir::Arg src2)
    {
        bool extended = false;
        std::uint32_t extension;
        data_.push_back((0b0111110U << 25) | (static_cast<unsigned>(opCode) << 17) | (getVGPR(src2) << 9) |
                        getVSRC(src1, extended, extension));
        if (extended)
            data_.push_back(extension);
    }

    void encodeVOP1(VOP1OpCode opCode, lir::Arg dest, lir::Arg src)
    {
        bool extended = false;
        std::uint32_t extension;
        data_.push_back((0b0111111U << 25) | (getVGPR(dest) << 17) | (static_cast<unsigned>(opCode) << 9) |
                        getVSRC(src, extended, extension));
        if (extended)
            data_.push_back(extension);
    }

    void encodeVINTRP(VINTRPOpCode opCode, unsigned attribute, unsigned channel, lir::Arg dest, lir::Arg src)
    {
        data_.push_back((0b110101U << 26) | (getVGPR(dest) << 18) | (static_cast<unsigned>(opCode) << 16) |
                        (attribute << 10) | (channel << 8) | getVGPR(src));
    }

    void encodeVINTRP(VINTRPOpCode opCode, unsigned attribute, unsigned channel, lir::Arg dest, unsigned point)
    {
        data_.push_back((0b110101U << 26) | (getVGPR(dest) << 18) | (static_cast<unsigned>(opCode) << 16) |
                        (attribute << 10) | (channel << 8) | point);
    }

    void encodeEXP(unsigned enable, unsigned target, bool compressed, bool done, bool validMask, lir::Arg op1,
                   lir::Arg op2, lir::Arg op3, lir::Arg op4)
    {
        data_.push_back((0b110001U << 26) | enable | (target << 4) | ((compressed ? 1 : 0) << 10) |
                        ((done ? 1 : 0) << 11) | ((validMask ? 1 : 0) << 12));
        data_.push_back(getVGPR(op1) | (getVGPR(op2) << 8) | (getVGPR(op3) << 16) | (getVGPR(op4) << 24));
    }

    std::vector<std::uint32_t> const& data() const { return data_; }

  private:
    Label killLabel_;
    std::unordered_map<lir::Block const*, Label> blockLabels_;
    std::vector<std::uint32_t> data_;
};

bool
overlap(lir::Arg const& a, lir::Arg const& b)
{
    if (!a.isTemp() || !b.isTemp())
        return false;
    auto aStart = a.physReg().reg * 4;
    auto aEnd = aStart + a.size();
    auto bStart = b.physReg().reg * 4;
    auto bEnd = bStart + b.size();

    return aEnd > bStart && aStart < bEnd;
}

void
emitParallelCopy(Emitter& em, lir::Inst& insn)
{
    auto count = insn.definitionCount();
    std::vector<std::pair<lir::Arg, lir::Arg>> copies;

    for (unsigned i = 0; i < count; ++i) {
        auto const& op = insn.getOperand(i);
        auto const& def = insn.getDefinition(i);

        if (op.isTemp() && op.physReg().reg == def.physReg().reg)
            continue;

        if (!op.isTemp())
            std::terminate();
        copies.push_back({op, def});
    }

    while (!copies.empty()) {
        bool progress = false;
        for (std::size_t i = 0; i < copies.size(); ++i) {
            bool allowed = true;
            for (std::size_t j = 0; j < copies.size(); ++j) {
                if (i != j && overlap(copies[i].second, copies[j].first))
                    allowed = false;
            }
            if (allowed) {
                progress = true;
                auto const& op = copies[i].first;
                auto const& def = copies[i].second;

                if (op.regClass() == lir::RegClass::sgpr && def.regClass() == lir::RegClass::sgpr) {
                    if (op.size() == 4)
                        em.encodeSOP1(SOP1OpCode::s_mov_b32, def, op);
                    else
                        std::terminate();
                } else if (def.regClass() == lir::RegClass::vgpr) {
                    if (op.size() == 4)
                        em.encodeVOP1(VOP1OpCode::v_mov_b32, def, op);
                    else
                        std::terminate();
                } else
                    std::terminate();

                copies.erase(copies.begin() + i);
                --i;
            }
        }
        if (!progress)
            std::terminate();
    }
}
}
void
emit(lir::Program& program)
{
    Emitter em;
    for (auto& bb : program.blocks()) {
        em.startBlock(*bb);
        for (auto& insn : bb->instructions()) {
            switch (insn->opCode()) {
                case lir::OpCode::parallel_copy:
                    emitParallelCopy(em, *insn);
                    break;
                case lir::OpCode::v_interp_p1_f32:
                    em.encodeVINTRP(VINTRPOpCode::v_interp_p1_f32, insn->aux().vintrp.attribute,
                                    insn->aux().vintrp.channel, insn->getDefinition(0), insn->getOperand(0));
                    break;
                case lir::OpCode::v_interp_p2_f32:
                    em.encodeVINTRP(VINTRPOpCode::v_interp_p2_f32, insn->aux().vintrp.attribute,
                                    insn->aux().vintrp.channel, insn->getDefinition(0), insn->getOperand(1));
                    break;
                case lir::OpCode::exp:
                    em.encodeEXP(insn->aux().exp.enable, insn->aux().exp.target, insn->aux().exp.compressed,
                                 insn->aux().exp.done, insn->aux().exp.validMask, insn->getOperand(0),
                                 insn->getOperand(1), insn->getOperand(2), insn->getOperand(3));
                    break;
                case lir::OpCode::s_endpgm:
                    em.encodeSOPP(SOPPOpCode::s_endpgm, 0);
                    break;
                case lir::OpCode::start:
                    break;
                case lir::OpCode::start_block: {
                    if (insn->operandCount() == 0)
                        break;
                    if (insn->operandCount() == 1) {
                        em.encodeSOP1(SOP1OpCode::s_mov_b64,
                                      lir::Arg{lir::Temp{~0U, lir::RegClass::sgpr, 8}, lir::PhysReg{126}},
                                      insn->getOperand(0));
                        break;
                    }
                    em.encodeSOP2(SOP2OpCode::s_or_b64,
                                  lir::Arg{lir::Temp{~0U, lir::RegClass::sgpr, 8}, lir::PhysReg{126}},
                                  insn->getOperand(0), insn->getOperand(1));
                    for (unsigned i = 2; i < insn->operandCount(); ++i)
                        em.encodeSOP2(
                          SOP2OpCode::s_or_b64, lir::Arg{lir::Temp{~0U, lir::RegClass::sgpr, 8}, lir::PhysReg{126}},
                          lir::Arg{lir::Temp{~0U, lir::RegClass::sgpr, 8}, lir::PhysReg{126}}, insn->getOperand(i));

                } break;
                case lir::OpCode::v_cmp_lt_f32:
                    em.encodeVOPC(VOPCOpCode::v_cmp_lt_f32, insn->getOperand(0), insn->getOperand(1));
                    break;
                case lir::OpCode::logical_branch:
                    em.encodeSOP1(SOP1OpCode::s_mov_b64, insn->getDefinition(0),
                                  lir::Arg{lir::Temp{~0U, lir::RegClass::sgpr, 8}, lir::PhysReg{126}});
                    break;
                case lir::OpCode::logical_cond_branch:
                    if (overlap(insn->getDefinition(0), insn->getOperand(0))) {
                        em.encodeSOP2(SOP2OpCode::s_andn2_b64, insn->getDefinition(1),
                                      lir::Arg{lir::Temp{~0U, lir::RegClass::sgpr, 8}, lir::PhysReg{126}},
                                      insn->getOperand(0));
                        em.encodeSOP2(SOP2OpCode::s_and_b64, insn->getDefinition(0),
                                      lir::Arg{lir::Temp{~0U, lir::RegClass::sgpr, 8}, lir::PhysReg{126}},
                                      insn->getOperand(0));
                    } else {
                        em.encodeSOP2(SOP2OpCode::s_and_b64, insn->getDefinition(0),
                                      lir::Arg{lir::Temp{~0U, lir::RegClass::sgpr, 8}, lir::PhysReg{126}},
                                      insn->getOperand(0));
                        em.encodeSOP2(SOP2OpCode::s_andn2_b64, insn->getDefinition(1),
                                      lir::Arg{lir::Temp{~0U, lir::RegClass::sgpr, 8}, lir::PhysReg{126}},
                                      insn->getOperand(0));
                    }
                    break;
                case lir::OpCode::phi:
                    break;
                default:
                    std::terminate();
            }
        }
    }
    std::ofstream os("test.bin");
    auto const& vec = em.data();
    os.write(static_cast<char const*>(static_cast<void const*>(vec.data())), vec.size() * sizeof(std::uint32_t));
}
}
}
