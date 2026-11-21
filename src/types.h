// types.h — Part 3c: the static types novac tracks. Unknown means "can't tell
// statically" (function parameters, values from user functions) and is
// permissive everywhere; real per-call-site inference is a codegen concern.
#pragma once
#include <string>

namespace novac {

enum class TypeKind { Unknown, Void, Int, Float, Bool, String, Array };

struct Type {
    TypeKind kind = TypeKind::Unknown;
    TypeKind elem = TypeKind::Unknown; // element kind when kind == Array
    Type() = default;
    Type(TypeKind k, TypeKind e = TypeKind::Unknown) : kind(k), elem(e) {}
    bool unknown() const { return kind == TypeKind::Unknown; }
    bool numeric() const { return kind == TypeKind::Int || kind == TypeKind::Float; }
};

inline const char* kind_name(TypeKind k) {
    switch (k) {
        case TypeKind::Unknown: return "unknown";
        case TypeKind::Void: return "void";
        case TypeKind::Int: return "int";
        case TypeKind::Float: return "float";
        case TypeKind::Bool: return "bool";
        case TypeKind::String: return "string";
        case TypeKind::Array: return "array";
    }
    return "?";
}

inline std::string type_name(const Type& t) {
    if (t.kind == TypeKind::Array && t.elem != TypeKind::Unknown)
        return std::string("array of ") + kind_name(t.elem);
    return kind_name(t.kind);
}

inline bool types_equal(const Type& a, const Type& b) { return a.kind == b.kind && a.elem == b.elem; }

// Can a value of type `src` be stored into something of type `dst`?
inline bool assignable(const Type& dst, const Type& src) {
    if (dst.unknown() || src.unknown()) return true;
    if (dst.kind == TypeKind::Float && src.kind == TypeKind::Int) return true;
    if (dst.kind != src.kind) return false;
    if (dst.kind == TypeKind::Array) {
        return dst.elem == TypeKind::Unknown || src.elem == TypeKind::Unknown || dst.elem == src.elem ||
               (dst.elem == TypeKind::Float && src.elem == TypeKind::Int);
    }
    return true;
}

} // namespace novac
