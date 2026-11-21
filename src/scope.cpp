#include "scope.h"

namespace novac {

bool Scope::declare(const Symbol& sym) {
    if (symbols_.count(sym.name)) return false;
    symbols_[sym.name] = sym;
    return true;
}

const Symbol* Scope::resolve(const std::string& name) const {
    if (const Symbol* s = resolve_local(name)) return s;
    return parent_ ? parent_->resolve(name) : nullptr;
}

const Symbol* Scope::resolve_local(const std::string& name) const {
    auto it = symbols_.find(name);
    return it != symbols_.end() ? &it->second : nullptr;
}

} // namespace novac
