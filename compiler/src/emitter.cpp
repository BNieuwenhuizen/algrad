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

    friend class Encoder;
};

unsigned
getVGPR(lir::Arg arg)
{
    assert(arg.is_temp() && arg.isFixed());
    auto v = arg.physReg();

    assert(v.reg >= 256);
    return v.reg - 256u;
}

unsigned
getSGPR(lir::Arg arg)
{
    assert(arg.is_temp() && arg.isFixed());
    auto v = arg.physReg();

    assert(v.reg < 128);
    return v.reg;
}

unsigned
getSSRC(lir::Arg arg, bool& extended, std::uint32_t& extension)
{
    if (arg.is_temp()) {
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
    if (arg.is_temp()) {
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

struct ssrc
{
    unsigned value;
    std::uint32_t constant;
};

struct vsrc
{
    unsigned value;
    std::uint32_t constant;
};

struct sgpr
{
    unsigned value;
};

struct vgpr
{
    unsigned value;
};

class Encoder
{
  public:
    Encoder() {}

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

    void encodeSOP2(SOP2OpCode opCode, sgpr dest, ssrc src1, ssrc src2)
    {
        assert(src1.value != 255 || src2.value != 255);
        data_.push_back((0b10U << 30) | (static_cast<unsigned>(opCode) << 23) | (dest.value << 16) | (src2.value << 8) |
                        src1.value);
        if (src1.value == 255)
            data_.push_back(src1.constant);
        else if (src2.value == 255)
            data_.push_back(src2.constant);
    }

    void encodeSOP1(SOP1OpCode opCode, sgpr dest, ssrc src)
    {
        data_.push_back((0b101111101U << 23) | (dest.value << 16) | (static_cast<unsigned>(opCode) << 8) | src.value);
        if (src.value == 255)
            data_.push_back(src.constant);
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

    void encodeVOPC(VOPCOpCode opCode, vsrc src1, vgpr src2)
    {
        data_.push_back((0b0111110U << 25) | (static_cast<unsigned>(opCode) << 17) | (src2.value << 9) | src1.value);
        if (src1.value == 255)
            data_.push_back(src1.constant);
    }

    void encodeVOP1(VOP1OpCode opCode, vgpr dest, vsrc src)
    {
        data_.push_back((0b0111111U << 25) | (dest.value << 17) | (static_cast<unsigned>(opCode) << 9) | src.value);
        if (src.value == 255)
            data_.push_back(src.constant);
    }

    void encodeVINTRP(VINTRPOpCode opCode, unsigned attribute, unsigned channel, vgpr dest, vgpr src)
    {
        data_.push_back((0b110101U << 26) | (dest.value << 18) | (static_cast<unsigned>(opCode) << 16) |
                        (attribute << 10) | (channel << 8) | src.value);
    }

    void encodeVINTRP(VINTRPOpCode opCode, unsigned attribute, unsigned channel, vgpr dest, unsigned point)
    {
        data_.push_back((0b110101U << 26) | (dest.value << 18) | (static_cast<unsigned>(opCode) << 16) |
                        (attribute << 10) | (channel << 8) | point);
    }

    void encodeEXP(unsigned enable, unsigned target, bool compressed, bool done, bool validMask, vgpr op1, vgpr op2,
                   vgpr op3, vgpr op4)
    {
        data_.push_back((0b110001U << 26) | enable | (target << 4) | ((compressed ? 1 : 0) << 10) |
                        ((done ? 1 : 0) << 11) | ((validMask ? 1 : 0) << 12));
        data_.push_back(op1.value | (op2.value << 8) | (op3.value << 16) | (op4.value << 24));
    }

    std::vector<std::uint32_t> const& data() const { return data_; }

  private:
    Label killLabel_;
    std::unordered_map<lir::Block const*, Label> blockLabels_;
    std::vector<std::uint32_t> data_;
};
class Emitter
{
  public:
    Emitter(lir::Program& program)
      : program{&program}
    {
    }

    void emitEXP(lir::Inst& inst)
    {
        encoder.encodeEXP(inst.aux().exp.enable, inst.aux().exp.target, inst.aux().exp.compressed, inst.aux().exp.done,
                          inst.aux().exp.validMask, make_vgpr(inst.getOperand(0)), make_vgpr(inst.getOperand(1)),
                          make_vgpr(inst.getOperand(2)), make_vgpr(inst.getOperand(3)));
    }

    std::vector<std::uint32_t> const& data() const noexcept { return encoder.data(); }

    void run()
    {
        for (auto& bb : program->blocks()) {
            encoder.startBlock(*bb);
            for (auto& insn : bb->instructions()) {
                switch (insn->opCode()) {
                    case lir::OpCode::parallel_copy:
                        emitParallelCopy(*insn);
                        break;
                    case lir::OpCode::v_interp_p1_f32:
                        encoder.encodeVINTRP(VINTRPOpCode::v_interp_p1_f32, insn->aux().vintrp.attribute,
                                             insn->aux().vintrp.channel, make_vgpr(insn->getDefinition(0)),
                                             make_vgpr(insn->getOperand(0)));
                        break;
                    case lir::OpCode::v_interp_p2_f32:
                        encoder.encodeVINTRP(VINTRPOpCode::v_interp_p2_f32, insn->aux().vintrp.attribute,
                                             insn->aux().vintrp.channel, make_vgpr(insn->getDefinition(0)),
                                             make_vgpr(insn->getOperand(1)));
                        break;
                    case lir::OpCode::exp:
                        emitEXP(*insn);
                        break;
                    case lir::OpCode::s_endpgm:
                        encoder.encodeSOPP(SOPPOpCode::s_endpgm, 0);
                        break;
                    case lir::OpCode::start:
                        break;
                    case lir::OpCode::start_block: {
                        if (insn->operandCount() == 0)
                            break;
                        if (insn->operandCount() == 1) {
                            encoder.encodeSOP1(SOP1OpCode::s_mov_b64, sgpr{126}, make_ssrc(insn->getOperand(0)));
                            break;
                        }
                        encoder.encodeSOP2(SOP2OpCode::s_or_b64, sgpr{126}, make_ssrc(insn->getOperand(0)),
                                           make_ssrc(insn->getOperand(1)));
                        for (unsigned i = 2; i < insn->operandCount(); ++i)
                            encoder.encodeSOP2(SOP2OpCode::s_or_b64, sgpr{126}, ssrc{126, 0},
                                               make_ssrc(insn->getOperand(i)));

                    } break;
                    case lir::OpCode::v_cmp_lt_f32:
                        encoder.encodeVOPC(VOPCOpCode::v_cmp_lt_f32, make_vsrc(insn->getOperand(0)),
                                           make_vgpr(insn->getOperand(1)));
                        break;
                    case lir::OpCode::logical_branch:
                        encoder.encodeSOP1(SOP1OpCode::s_mov_b64, make_sgpr(insn->getDefinition(0)), ssrc{126, 0});
                        break;
                    case lir::OpCode::logical_cond_branch:
                        if (overlap(insn->getDefinition(0), insn->getOperand(0))) {
                            encoder.encodeSOP2(SOP2OpCode::s_andn2_b64, make_sgpr(insn->getDefinition(1)), ssrc{126, 0},
                                               make_ssrc(insn->getOperand(0)));
                            encoder.encodeSOP2(SOP2OpCode::s_and_b64, make_sgpr(insn->getDefinition(0)), ssrc{126, 0},
                                               make_ssrc(insn->getOperand(0)));
                        } else {
                            encoder.encodeSOP2(SOP2OpCode::s_and_b64, make_sgpr(insn->getDefinition(0)), ssrc{126, 0},
                                               make_ssrc(insn->getOperand(0)));
                            encoder.encodeSOP2(SOP2OpCode::s_andn2_b64, make_sgpr(insn->getDefinition(1)), ssrc{126, 0},
                                               make_ssrc(insn->getOperand(0)));
                        }
                        break;
                    case lir::OpCode::phi:
                        break;
                    default:
                        std::terminate();
                }
            }
        }
    }

  private:
    void emitParallelCopy(lir::Inst& insn);

    bool overlap(lir::Arg const& a, lir::Arg const& b) const noexcept
    {
        if (!a.is_temp() || !b.is_temp())
            return false;
        auto aStart = a.physReg().reg;
        auto aEnd = aStart + program->temp_info(a.temp()).size;
        auto bStart = b.physReg().reg;
        auto bEnd = bStart + program->temp_info(b.temp()).size;

        return aEnd > bStart && aStart < bEnd;
    }

    ssrc make_ssrc(lir::Arg arg) const noexcept
    {
        if (arg.is_temp()) {
            assert(arg.is_temp() && arg.isFixed());
            assert(!(arg.physReg().reg & 3) && arg.physReg().reg < 1024);
            return ssrc{arg.physReg().reg / 4, 0};
        } else {
            return ssrc{255, arg.constantValue()};
        }
    }

    vsrc make_vsrc(lir::Arg arg) const noexcept
    {
        if (arg.is_temp()) {
            assert(arg.is_temp() && arg.isFixed());
            assert(!(arg.physReg().reg & 3));
            return vsrc{arg.physReg().reg / 4, 0};
        } else {
            return vsrc{255, arg.constantValue()};
        }
    }

    sgpr make_sgpr(lir::Arg arg) const noexcept
    {
        assert(arg.is_temp() && arg.isFixed());
        assert(!(arg.physReg().reg & 3) && arg.physReg().reg < 1024);
        return sgpr{arg.physReg().reg / 4};
    }

    vgpr make_vgpr(lir::Arg arg) const noexcept
    {
        assert(arg.is_temp() && arg.isFixed());
        assert(!(arg.physReg().reg & 3) && arg.physReg().reg >= 1024);
        return vgpr{arg.physReg().reg / 4 - 256};
    }
    Encoder encoder;
    lir::Program* program;
};

void
Emitter::emitParallelCopy(lir::Inst& insn)
{
    auto count = insn.definitionCount();
    std::vector<std::pair<lir::Arg, lir::Arg>> copies;

    for (unsigned i = 0; i < count; ++i) {
        auto const& op = insn.getOperand(i);
        auto const& def = insn.getDefinition(i);

        if (op.is_temp() && op.physReg().reg == def.physReg().reg)
            continue;

        if (!op.is_temp())
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

                if (program->temp_info(op.temp()).reg_class == lir::RegClass::sgpr &&
                    program->temp_info(def.temp()).reg_class == lir::RegClass::sgpr) {
                    if (program->temp_info(op.temp()).size == 4)
                        encoder.encodeSOP1(SOP1OpCode::s_mov_b32, make_sgpr(def), make_ssrc(op));
                    else
                        std::terminate();
                } else if (program->temp_info(def.temp()).reg_class == lir::RegClass::vgpr) {
                    if (program->temp_info(op.temp()).size == 4)
                        encoder.encodeVOP1(VOP1OpCode::v_mov_b32, make_vgpr(def), make_vsrc(op));
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
    Emitter em(program);
    em.run();
    std::ofstream os("test.bin");
    auto const& vec = em.data();
    os.write(static_cast<char const*>(static_cast<void const*>(vec.data())), vec.size() * sizeof(std::uint32_t));
}
}
}
