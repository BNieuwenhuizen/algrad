#include "types.hpp"

namespace algrad {
namespace compiler {

TypeInfo const voidType{TypeKind::none};
TypeInfo const boolType{TypeKind::boolean};
ScalarTypeInfo const int16Type{TypeKind::integer, 16};
ScalarTypeInfo const int32Type{TypeKind::integer, 32};
ScalarTypeInfo const int64Type{TypeKind::integer, 64};
ScalarTypeInfo const float16Type{TypeKind::floatingPoint, 16};
ScalarTypeInfo const float32Type{TypeKind::floatingPoint, 32};
ScalarTypeInfo const float64Type{TypeKind::floatingPoint, 64};

Type
intType(unsigned width) noexcept
{
    switch (width) {
        case 16:
            return &int16Type;
        case 32:
            return &int32Type;
        case 64:
            return &int64Type;
        default:
            std::abort();
    }
}

Type
floatType(unsigned width) noexcept
{
    switch (width) {
        case 16:
            return &float16Type;
        case 32:
            return &float32Type;
        case 64:
            return &float64Type;
        default:
            std::abort();
    }
}

void
TypeContext::TypeInfoDeleter::operator()(TypeInfo* ti) noexcept
{
    if (!ti)
        return;
    switch (ti->kind()) {
        case TypeKind::none:
        case TypeKind::boolean:
            delete ti;
            break;
        case TypeKind::integer:
        case TypeKind::floatingPoint:
            delete static_cast<ScalarTypeInfo*>(ti);
            break;
        case TypeKind::vector:
            delete static_cast<VectorTypeInfo*>(ti);
            break;
        case TypeKind::pointer:
            delete static_cast<PointerTypeInfo*>(ti);
            break;
    }
}

Type
TypeContext::vectorType(Type element, unsigned count)
{
    for (auto& ti : typeInfos_) {
        if (ti->kind() != TypeKind::vector)
            continue;

        auto& vti = static_cast<VectorTypeInfo&>(*ti);
        if (vti.element() == element && vti.size() == count)
            return &vti;
    }
    typeInfos_.push_back(std::unique_ptr<TypeInfo, TypeInfoDeleter>{new VectorTypeInfo{element, count}});
    return typeInfos_.back().get();
}

Type
TypeContext::pointerType(Type pointee, StorageKind storage)
{
    for (auto& ti : typeInfos_) {
        if (ti->kind() != TypeKind::pointer)
            continue;

        auto& pti = static_cast<PointerTypeInfo&>(*ti);
        if (pti.pointeeType() == pointee && pti.storage() == storage)
            return &pti;
    }
    typeInfos_.push_back(std::unique_ptr<TypeInfo, TypeInfoDeleter>{new PointerTypeInfo{pointee, storage}});
    return typeInfos_.back().get();
}

bool
isComposite(Type t) noexcept
{
    switch (t->kind()) {
        case TypeKind::array:
        case TypeKind::structure:
        case TypeKind::vector:
        case TypeKind::matrix:
        default:
            return true;
        case TypeKind::boolean:
        case TypeKind::none:
        case TypeKind::integer:
        case TypeKind::floatingPoint:
        case TypeKind::pointer:
            return false;
    }
}

std::size_t
compositeCount(Type t) noexcept
{
    switch (t->kind()) {
        case TypeKind::vector:
            return static_cast<VectorTypeInfo const*>(t)->size();
    }
}

Type
compositeType(Type t, std::size_t index) noexcept
{
    switch (t->kind()) {
        case TypeKind::vector:
            return static_cast<VectorTypeInfo const*>(t)->element();
    }
}
}
}
