// modules.h — Part 3d: import resolution + tree-shaking over the AST.
// Loads every imported library (recursively), keeps a pool of its functions,
// and hands back only the ones the program actually reaches through calls.
#pragma once

#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>
#include "ast.h"

namespace novac {

struct ModuleError {
    std::string message;
    std::string file; // library the error is in; empty = the importing (main) file
    int line;
    int col;
};

class ModuleLoader {
public:
    // Resolves `imports` relative to base_dir (or their own `From` dir) and
    // parses each library once. Re-importing a file is a no-op, which also
    // makes import cycles harmless.
    void load(const std::vector<StmtPtr>& imports, const std::string& base_dir);

    // Moves out the library functions reachable from `program`'s calls
    // (following call chains through the libraries themselves).
    std::vector<StmtPtr> take_reachable(const ProgramStmt& program);

    const std::vector<ModuleError>& errors() const { return errors_; }
    bool had_failures() const { return failed_; } // a file was missing or didn't parse
    size_t pool_size() const { return pool_.size(); }

private:
    std::vector<std::unique_ptr<ProgramStmt>> programs_;
    std::map<std::string, FuncDeclStmt*> pool_;
    std::set<std::string> visited_;
    std::vector<ModuleError> errors_;
    bool failed_ = false;

    void load_imports(const std::vector<StmtPtr>& imports, const std::string& base_dir, const std::string& importer);
    void add_error(const std::string& message, const std::string& file, int line, int col);
};

} // namespace novac
