#ifndef ALGRAD_COMPILER__TYPES_HPP
#define ALGRAD_COMPILER__TYPES_HPP

#include <cassert>
#include <cstdint>
#include <memory>
#include <vector>

namespace algrad {
namespace compiler {

enum class TypeKind
{
    none,
    boolean,
    integer,
    floatingPoint,
    vector,
    matrix,
    pointer,
    array,
    structure,
    function
};

enum class StorageKind
{
    global,
    workgroup,
    invocation,
    function,
    uniform,
    uniformConstant,
    pushConstant,
    atomic,
    image,
    generic
};

class TypeInfo
{
  public:
    constexpr TypeInfo(TypeKind kind) noexcept;
    ~TypeInfo() = default;

    constexpr TypeKind kind() const noexcept;

  private:
    TypeKind kind_;
};

using Type = TypeInfo const*;

class ScalarTypeInfo final : public TypeInfo
{
  public:
    constexpr ScalarTypeInfo(TypeKind kind, unsigned width) noexcept;

    constexpr unsigned width() const noexcept;

  private:
    unsigned width_;
};

class VectorTypeInfo final : public TypeInfo
{
  public:
    constexpr VectorTypeInfo(Type element, unsigned size) noexcept;

    constexpr Type element() const noexcept;
    constexpr unsigned size() const noexcept;

  private:
    unsigned size_;
    Type element_;
};

class PointerTypeInfo final : public TypeInfo
{
  public:
    constexpr PointerTypeInfo(Type pointeeType, StorageKind storage) noexcept;

    constexpr StorageKind storage() const noexcept;
    constexpr Type pointeeType() const noexcept;

  private:
    StorageKind storage_;
    Type pointeeType_;
};

class FunctionTypeInfo : public TypeInfo
{
  public:
    FunctionTypeInfo(Type returnType, std::vector<Type> argumentTypes);

    Type returnType() const noexcept;
    std::vector<Type> const& argumentTypes() const noexcept;

  private:
    Type returnType_;
    std::vector<Type> argumentTypes_;
};

class TypeContext
{
  public:
    Type vectorType(Type element, unsigned count);
    Type pointerType(Type pointee, StorageKind storage);
    Type functionType(Type returnType, std::vector<Type> argTypes);

  private:
    struct TypeInfoDeleter
    {
        void operator()(TypeInfo*) noexcept;
    };

    std::vector<std::unique_ptr<TypeInfo, TypeInfoDeleter>> typeInfos_;
};

extern TypeInfo const voidType;
extern TypeInfo const boolType;
extern ScalarTypeInfo const int16Type;
extern ScalarTypeInfo const int32Type;
extern ScalarTypeInfo const int64Type;
extern ScalarTypeInfo const float16Type;
extern ScalarTypeInfo const float32Type;
extern ScalarTypeInfo const float64Type;

Type intType(unsigned width) noexcept;
Type floatType(unsigned width) noexcept;

bool isComposite(Type t) noexcept;
std::size_t compositeCount(Type t) noexcept;
Type compositeType(Type t, std::size_t index) noexcept;

constexpr TypeInfo::TypeInfo(TypeKind kind) noexcept : kind_{kind}
{
}

constexpr TypeKind
TypeInfo::kind() const noexcept
{
    return kind_;
}

constexpr ScalarTypeInfo::ScalarTypeInfo(TypeKind kind, unsigned width) noexcept : TypeInfo{kind}, width_{width}
{
}

constexpr unsigned
ScalarTypeInfo::width() const noexcept
{
    return width_;
}

constexpr VectorTypeInfo::VectorTypeInfo(Type element, unsigned size) noexcept : TypeInfo{TypeKind::vector},
                                                                                 size_{size},
                                                                                 element_{element}

{
}

constexpr Type
VectorTypeInfo::element() const noexcept
{
    return element_;
}

constexpr unsigned
VectorTypeInfo::size() const noexcept
{
    return size_;
}

constexpr PointerTypeInfo::PointerTypeInfo(Type pointeeType, StorageKind storage) noexcept
  : TypeInfo{TypeKind::pointer},
    storage_{storage},
    pointeeType_{pointeeType}
{
}

constexpr StorageKind
PointerTypeInfo::storage() const noexcept
{
    return storage_;
}

constexpr Type
PointerTypeInfo::pointeeType() const noexcept
{
    return pointeeType_;
}

inline Type
FunctionTypeInfo::returnType() const noexcept
{
    return returnType_;
}

inline std::vector<Type> const&
FunctionTypeInfo::argumentTypes() const noexcept
{
    return argumentTypes_;
}
}
}

#endif
