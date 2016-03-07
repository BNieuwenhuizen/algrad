#include "spirv_loader.hpp"

#include "hir.hpp"
#include "hir_inlines.hpp"

#include "types.hpp"

#include "spirv.hpp"

#include <algorithm>
#include <cstring>
#include <iostream>
#include <unordered_map>

#include <boost/range/iterator_range.hpp>

namespace algrad {
namespace compiler {

using namespace hir;

namespace {

unsigned
wordCount(std::uint32_t v)
{
    return v >> spv::WordCountShift;
}

spv::Op
opCode(std::uint32_t v)
{
    return static_cast<spv::Op>(v & spv::OpCodeMask);
}

ProgramType
toProgramType(spv::ExecutionModel model)
{
    switch (model) {
        case spv::ExecutionModel::Fragment:
            return ProgramType::fragment;
        case spv::ExecutionModel::Vertex:
            return ProgramType::vertex;
        case spv::ExecutionModel::GLCompute:
            return ProgramType::compute;
        default:
            throw - 1;
    }
}

StorageKind
toStorageKind(spv::StorageClass s)
{
    switch (s) {
        case spv::StorageClass::Function:
        case spv::StorageClass::Private:
        case spv::StorageClass::Input:
        case spv::StorageClass::Output:
            return StorageKind::invocation;
        default:
            std::abort();
    }
}

struct SPIRVObject
{
    SPIRVObject() noexcept;
    enum class Tag
    {
        none,
        type,
        lazy_var,
        def
    } tag;
    union
    {
        Type type;
        Def* def;
    };
    std::uint32_t const* definition;
};

struct SPIRVBuilder
{
    std::string entryName;
    unsigned entryId;
    std::vector<unsigned> ioVars;

    std::unique_ptr<hir::Program> program;
    std::vector<SPIRVObject> objects;
    std::vector<std::pair<unsigned, Def *>> inputs, outputs;

    std::unordered_map<unsigned, std::pair<std::uint32_t const*, std::uint32_t const*>> functionStarts;

    unsigned currFunctionId;
};

SPIRVObject::SPIRVObject() noexcept : tag{Tag::none}
{
}

template <typename Range, typename F, typename... Args>
auto
visitSPIRV(Range r, F&& callback, Args&&... args)
{
    auto cur = r.begin();
    auto e = r.end();
    while (cur != e) {
        auto size = wordCount(*cur);
        assert(size);
        if (!callback(boost::iterator_range<decltype(cur)>{cur, cur + size}, args...))
            return cur;
        if (!size)
            std::terminate();
        cur += size;
    }
    return cur;
}

std::pair<std::string, std::uint32_t const*>
literalString(boost::iterator_range<std::uint32_t const*> r)
{
    char const* b = static_cast<char const*>(static_cast<void const*>(r.begin()));
    char const* e = static_cast<char const*>(static_cast<void const*>(r.end()));
    auto it = std::find(b, e, 0);

    return {std::string(b, it), r.begin() + (it - b + 4) / 4};
}

bool
visitPreamble(boost::iterator_range<std::uint32_t const*> insn, SPIRVBuilder& builder)
{
    switch (opCode(insn.front())) {
        case spv::Op::OpCapability: {
            switch (static_cast<spv::Capability>(insn.begin()[1])) {
                case spv::Capability::Shader:
                    break;
                default:
                    std::terminate();
            }
            return true;
        }
        case spv::Op::OpExtension:
            std::terminate();
        case spv::Op::OpExtInstImport: {
            auto name = literalString({insn.begin() + 2, insn.end()}).first;
            if (name != "GLSL.std.450")
                std::terminate();
            return true;
        }
        case spv::Op::OpMemoryModel: {
            return true;
        }
        case spv::Op::OpEntryPoint: {
            std::string name;
            std::uint32_t const* it;
            std::tie(name, it) = literalString({insn.begin() + 3, insn.end()});
            if (name == builder.entryName) {
                if (builder.program)
                    std::terminate();
                builder.program =
                  std::make_unique<hir::Program>(toProgramType(static_cast<spv::ExecutionModel>(insn.begin()[1])));
                builder.entryId = insn.begin()[2];

                for (; it < insn.end(); ++it)
                    builder.ioVars.push_back(*it);
            }
            // TODO implement entry point
            return true;
        }
        case spv::Op::OpExecutionMode: {
            if (builder.entryId == insn.begin()[1]) {
                // TODO implement execution mode
            }
            return true;
        }
        case spv::Op::OpString:
        case spv::Op::OpSource:
        case spv::Op::OpSourceExtension:
        case spv::Op::OpSourceContinued:
        case spv::Op::OpName:
        case spv::Op::OpMemberName:
            // unhandled debug instructions
            return true;
        case spv::Op::OpDecorate:
        case spv::Op::OpDecorationGroup:
        case spv::Op::OpGroupDecorate:
        case spv::Op::OpMemberDecorate:
        case spv::Op::OpGroupMemberDecorate:
            // TODO: implement decorations
            return true;
        default:
            return false;
    }
}

Type
getType(SPIRVBuilder& builder, unsigned id)
{
    if (id >= builder.objects.size() || builder.objects[id].tag != SPIRVObject::Tag::type)
        std::terminate();

    return builder.objects[id].type;
}

void
visitType(boost::iterator_range<std::uint32_t const*> insn, SPIRVBuilder& builder)
{
    Type type;
    auto id = insn.begin()[1];
    switch (opCode(insn.front())) {
        case spv::Op::OpTypeVoid:
            type = &voidType;
            break;
        case spv::Op::OpTypeBool:
            type = &boolType;
            break;
        case spv::Op::OpTypeInt:
            type = intType(insn.begin()[2]);
            break;
        case spv::Op::OpTypeFloat:
            type = floatType(insn.begin()[2]);
            break;
        case spv::Op::OpTypeVector:
            type = builder.program->types().vectorType(getType(builder, insn.begin()[2]), insn.begin()[3]);
            break;
        case spv::Op::OpTypePointer:
            type = builder.program->types().pointerType(getType(builder, insn.begin()[3]),
                                                        toStorageKind(static_cast<spv::StorageClass>(insn.begin()[2])));
            break;
        case spv::Op::OpTypeFunction: {
            auto returnType = getType(builder, insn.begin()[2]);
            std::vector<Type> args(insn.end() - insn.begin() - 3);
            for (std::size_t i = 0; i < args.size(); ++i)
                args[i] = getType(builder, insn.begin()[i + 3]);
            return;
        }
    }
    if (id >= builder.objects.size() || builder.objects[id].tag != SPIRVObject::Tag::none)
        std::terminate();

    builder.objects[id].tag = SPIRVObject::Tag::type;
    builder.objects[id].type = type;
}
void
insertConstant(boost::iterator_range<std::uint32_t const*> insn, SPIRVBuilder& builder)
{
    auto id = insn[2];
    auto type = getType(builder, insn[1]);
    hir::ScalarConstant* def;
    if (type->kind() == TypeKind::integer || type->kind() == TypeKind::floatingPoint) {
        switch (static_cast<ScalarTypeInfo const*>(type)->width()) {
            case 16: {
                std::uint16_t v;
                std::memcpy(&v, &insn[3], 2);
                def = builder.program->getScalarConstant(type, static_cast<std::uint64_t>(v));
            } break;
            case 32: {
                std::uint32_t v;
                std::memcpy(&v, &insn[3], 4);
                def = builder.program->getScalarConstant(type, static_cast<std::uint64_t>(v));
            } break;
            case 64: {
                std::uint64_t v;
                std::memcpy(&v, &insn[3], 8);
                def = builder.program->getScalarConstant(type, static_cast<std::uint64_t>(v));
            } break;
        }
    } else
        std::terminate();
    builder.objects[id].tag = SPIRVObject::Tag::def;
    builder.objects[id].def = def;
}

bool
visitGlobals(boost::iterator_range<std::uint32_t const*> insn, SPIRVBuilder& builder)
{
    switch (opCode(insn.front())) {
        case spv::Op::OpTypeVoid:
        case spv::Op::OpTypeBool:
        case spv::Op::OpTypeInt:
        case spv::Op::OpTypeFloat:
        case spv::Op::OpTypeVector:
        case spv::Op::OpTypePointer:
        case spv::Op::OpTypeFunction:
            visitType(insn, builder);
            return true;
        case spv::Op::OpConstant:
            insertConstant(insn, builder);
            return true;
        case spv::Op::OpConstantFalse:
        case spv::Op::OpConstantTrue:
        case spv::Op::OpConstantNull:
        case spv::Op::OpConstantComposite:
        case spv::Op::OpConstantSampler:
            return true;
        case spv::Op::OpVariable: {
            auto id = insn.begin()[2];
            if (id >= builder.objects.size() || builder.objects[id].tag != SPIRVObject::Tag::none)
                std::terminate();

            builder.objects[id].tag = SPIRVObject::Tag::lazy_var;
            builder.objects[id].definition = insn.begin();
            return true;
        }
        default:
            return false;
    }
}

bool
previsitFunctions(boost::iterator_range<std::uint32_t const*> insn, SPIRVBuilder& builder)
{
    switch (opCode(insn.front())) {
        case spv::Op::OpFunction: {
            builder.currFunctionId = insn.begin()[2];
            builder.functionStarts[builder.currFunctionId].first = insn.begin();
            return true;
        }
        case spv::Op::OpFunctionEnd: {
            builder.functionStarts.find(builder.currFunctionId)->second.second = insn.begin();
            return true;
        }
        default:
            return true;
    }
}

struct FunctionBuilder
{
    BasicBlock* currentBlock;
    BasicBlock* startBlock;
    std::unordered_map<unsigned, BasicBlock*> blocks;
};

Def*
getDef(SPIRVBuilder& builder, unsigned id)
{
    if (id >= builder.objects.size())
        std::terminate();
    if (builder.objects[id].tag == SPIRVObject::Tag::def)
        return builder.objects[id].def;
    else
        std::terminate();
}

void
createSimpleInstruction(boost::iterator_range<std::uint32_t const*> insn, SPIRVBuilder& builder, FunctionBuilder& fb,
                        OpCode opCode)
{
    auto type = getType(builder, insn.begin()[1]);
    auto id = insn.begin()[2];
    auto newInsn = builder.program->createDef<Inst>(opCode, type, insn.size() - 3);
    for (unsigned i = 0; i + 3 < insn.size(); ++i)
        newInsn->setOperand(i, getDef(builder, insn.begin()[i + 3]));

    builder.objects[id].tag = SPIRVObject::Tag::def;
    builder.objects[id].def = newInsn.get();

    fb.currentBlock->insertBack(std::move(newInsn));
}

void
createStoreInstruction(boost::iterator_range<std::uint32_t const*> insn, SPIRVBuilder& builder, FunctionBuilder& fb)
{
    auto newInsn = builder.program->createDef<Inst>(OpCode::store, &voidType, 2);
    for (unsigned i = 0; i < 2; ++i)
        newInsn->setOperand(i, getDef(builder, insn.begin()[i + 1]));

    fb.currentBlock->insertBack(std::move(newInsn));
}

void
createShuffleInstruction(boost::iterator_range<std::uint32_t const*> insn, SPIRVBuilder& builder, FunctionBuilder& fb)
{
    auto type = getType(builder, insn.begin()[1]);
    auto id = insn.begin()[2];
    auto newInsn = builder.program->createDef<Inst>(hir::OpCode::vectorShuffle, type, insn.size() - 3);
    for (unsigned i = 0; i < 2; ++i)
        newInsn->setOperand(i, getDef(builder, insn.begin()[i + 3]));

    for (unsigned i = 2; i + 3 < insn.size(); ++i)
        newInsn->setOperand(
          i, builder.program->getScalarConstant(&int32Type, static_cast<std::uint64_t>(insn.begin()[i + 3])));

    builder.objects[id].tag = SPIRVObject::Tag::def;
    builder.objects[id].def = newInsn.get();

    fb.currentBlock->insertBack(std::move(newInsn));
}

void
visitLabel(boost::iterator_range<std::uint32_t const*> insn, SPIRVBuilder& builder, FunctionBuilder& fb)
{
    auto id = insn[1];
    if (!fb.currentBlock) {
        fb.currentBlock = fb.startBlock;
        fb.blocks[id] = fb.startBlock;
        return;
    }

    auto it = fb.blocks.find(id);
    if (it == fb.blocks.end()) {
        hir::BasicBlock& b = builder.program->insertBack(builder.program->createBasicBlock());
        it = fb.blocks.insert({id, &b}).first;
    }
    fb.currentBlock = it->second;
}

hir::BasicBlock&
getBlock(SPIRVBuilder& builder, FunctionBuilder& fb, unsigned id)
{
    auto it = fb.blocks.find(id);
    if (it == fb.blocks.end()) {
        hir::BasicBlock& b = builder.program->insertBack(builder.program->createBasicBlock());
        it = fb.blocks.insert({id, &b}).first;
    }
    return *it->second;
}

void
visitBranch(boost::iterator_range<std::uint32_t const*> insn, SPIRVBuilder& builder, FunctionBuilder& fb)
{
    auto& block = getBlock(builder, fb, insn[1]);

    fb.currentBlock->insertBack(builder.program->createDef<Inst>(hir::OpCode::branch, &voidType, 0));
    fb.currentBlock->successors().push_back(&block);

    block.insertPredecessor(fb.currentBlock);
}

void
visitBranchConditional(boost::iterator_range<std::uint32_t const*> insn, SPIRVBuilder& builder, FunctionBuilder& fb)
{
    auto& trueBlock = getBlock(builder, fb, insn[2]);
    auto& falseBlock = getBlock(builder, fb, insn[3]);

    auto& inst = fb.currentBlock->insertBack(builder.program->createDef<Inst>(hir::OpCode::condBranch, &voidType, 1));
    inst.setOperand(0, getDef(builder, insn[1]));

    fb.currentBlock->successors().push_back(&trueBlock);
    fb.currentBlock->successors().push_back(&falseBlock);
    trueBlock.insertPredecessor(fb.currentBlock);
    falseBlock.insertPredecessor(fb.currentBlock);
}

bool
visitBody(boost::iterator_range<std::uint32_t const*> insn, SPIRVBuilder& builder, FunctionBuilder& fb)
{
    switch (opCode(insn.front())) {
        case spv::Op::OpFunction:
        case spv::Op::OpFunctionEnd:
            return true;
        case spv::Op::OpLabel:
            visitLabel(insn, builder, fb);
            return true;
        case spv::Op::OpBranchConditional:
            visitBranchConditional(insn, builder, fb);
            return true;
        case spv::Op::OpBranch:
            visitBranch(insn, builder, fb);
            return true;
        case spv::Op::OpReturn:
        case spv::Op::OpReturnValue:
            return true;
        case spv::Op::OpAccessChain:
            createSimpleInstruction(insn, builder, fb, OpCode::accessChain);
            return true;
        case spv::Op::OpLoad:
            createSimpleInstruction(insn, builder, fb, OpCode::load);
            return true;
        case spv::Op::OpStore:
            createStoreInstruction(insn, builder, fb);
            return true;
        case spv::Op::OpVectorShuffle:
            createShuffleInstruction(insn, builder, fb);
            return true;
        case spv::Op::OpFOrdLessThan:
            createSimpleInstruction(insn, builder, fb, OpCode::orderedLessThan);
            return true;
        case spv::Op::OpSelectionMerge:
        case spv::Op::OpLoopMerge:
            /* unused */
            return true;
        default:
            assert(0 && "unsupported instruction");
            std::terminate();
    }
}

BasicBlock&
visitFunction(SPIRVBuilder& builder, BasicBlock& startBlock, unsigned id, std::vector<Def*> const& args)
{
    auto loc = builder.functionStarts[id];
    FunctionBuilder fb;
    fb.startBlock = &startBlock;
    fb.currentBlock = nullptr;
    visitSPIRV(boost::iterator_range<std::uint32_t const*>{loc.first, loc.second}, visitBody, builder, fb);

    return *fb.currentBlock;
}

void
createIOVars(SPIRVBuilder& builder)
{
    for (auto id : builder.ioVars) {
        auto ptr = builder.objects[id].definition;
        auto type = getType(builder, ptr[1]);
        assert(type->kind() == TypeKind::pointer);
        auto v = builder.program->createDef<Inst>(OpCode::variable, type, 0);

        builder.objects[id].tag = SPIRVObject::Tag::def;
        builder.objects[id].def = v.get();

        if (static_cast<spv::StorageClass>(ptr[3]) == spv::StorageClass::Input)
            builder.inputs.push_back({id, v.get()});
        else
            builder.outputs.push_back({id, v.get()});
        builder.program->insertVariable(std::move(v));
    }
}

BasicBlock&
createProlog(SPIRVBuilder& builder)
{
    auto& bb = builder.program->insertBack(builder.program->createBasicBlock());

    for (auto vi : builder.inputs) {
        auto type = static_cast<PointerTypeInfo const*>(vi.second->type())->pointeeType();

        if (type->kind() == TypeKind::vector) {
            auto elemType = static_cast<VectorTypeInfo const*>(type)->element();
            for (unsigned i = 0; i < static_cast<VectorTypeInfo const*>(type)->size(); ++i) {
                auto& value =
                  builder.program->appendParam(builder.program->createDef<Inst>(OpCode::parameter, elemType, 0));

                auto elemPtrType = builder.program->types().pointerType(elemType, StorageKind::invocation);
                auto& accessChain =
                  bb.insertBack(builder.program->createDef<Inst>(OpCode::accessChain, elemPtrType, 2));
                accessChain.setOperand(0, vi.second);
                accessChain.setOperand(1,
                                       builder.program->getScalarConstant(&int32Type, static_cast<std::uint64_t>(i)));

                auto& store = bb.insertBack(builder.program->createDef<Inst>(OpCode::store, &voidType, 2));
                store.setOperand(0, &accessChain);
                store.setOperand(1, &value);
            }
        } else
            std::abort();
    }
    return bb;
}

void
createEpilog(SPIRVBuilder& builder, BasicBlock& bb)
{
    std::vector<Def*> defs;
    for (auto vi : builder.outputs) {
        auto type = static_cast<PointerTypeInfo const*>(vi.second->type())->pointeeType();

        if (type->kind() == TypeKind::vector) {
            auto elemType = static_cast<VectorTypeInfo const*>(type)->element();
            for (unsigned i = 0; i < static_cast<VectorTypeInfo const*>(type)->size(); ++i) {
                auto elemPtrType = builder.program->types().pointerType(elemType, StorageKind::invocation);
                auto& accessChain =
                  bb.insertBack(builder.program->createDef<Inst>(OpCode::accessChain, elemPtrType, 2));
                accessChain.setOperand(0, vi.second);
                accessChain.setOperand(1,
                                       builder.program->getScalarConstant(&int32Type, static_cast<std::uint64_t>(i)));

                auto& load = bb.insertBack(builder.program->createDef<Inst>(OpCode::load, elemType, 1));
                load.setOperand(0, &accessChain);
                defs.push_back(&load);
            }
        } else
            std::abort();
    }
    auto& ret = bb.insertBack(builder.program->createDef<Inst>(OpCode::ret, &voidType, defs.size()));
    for (unsigned i = 0; i < defs.size(); ++i) {
        ret.setOperand(i, defs[i]);
    }
}

void
visitEntryFunction(SPIRVBuilder& builder)
{

    createIOVars(builder);
    auto entryBlock = &createProlog(builder);

    auto& exitBlock = visitFunction(builder, *entryBlock, builder.entryId, {});
    createEpilog(builder, exitBlock);
}
}

std::unique_ptr<hir::Program>
loadSPIRV(std::uint32_t const* b, std::uint32_t const* e, std::string const& entryName)
{
    if (e - b < 5U)
        throw - 1;

    SPIRVBuilder builder;
    builder.objects.resize(b[3]);
    builder.entryName = entryName;
    auto cur = b + 5;
    cur = visitSPIRV(boost::iterator_range<std::uint32_t const*>{cur, e}, visitPreamble, builder);

    if (!builder.program)
        std::terminate();

    cur = visitSPIRV(boost::iterator_range<std::uint32_t const*>{cur, e}, visitGlobals, builder);
    cur = visitSPIRV(boost::iterator_range<std::uint32_t const*>{cur, e}, previsitFunctions, builder);

    visitEntryFunction(builder);
    return std::move(builder.program);
}
}
}
