// test_examples.cpp — end-to-end tests. For every tests/expected/<name>.out it builds
// tests/<name>.nvpp with novac, runs the executable (stdin from <name>.in if present)
// and compares stdout+stderr with <name>.out and the exit status with <name>.exit
// (default 0). Run from the repository root:  test_examples [path-to-novac]
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#ifndef _WIN32
#include <sys/wait.h>
#endif

namespace fs = std::filesystem;

#ifdef _WIN32
static const char* const EXE = ".exe";
static const char* const NULL_DEV = "NUL";
#else
static const char* const EXE = "";
static const char* const NULL_DEV = "/dev/null";
#endif

static int run_shell(const std::string& cmd) {
#ifdef _WIN32
    return std::system(("\"" + cmd + "\"").c_str()); // cmd.exe strips the outer quotes
#else
    int status = std::system(cmd.c_str());
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
#endif
}

static std::string read_text(const fs::path& p) { // line endings normalized to \n
    std::ifstream f(p, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    std::string out;
    for (char c : ss.str()) if (c != '\r') out += c;
    return out;
}

static std::string native(fs::path p) { return p.make_preferred().string(); }

int main(int argc, char** argv) {
    fs::path novac = argc > 1 ? fs::path(argv[1]) : fs::path("build") / (std::string("novac") + EXE);
    fs::path tests = "tests", work = fs::path("build") / "examples";
    if (!fs::exists(novac)) { std::cerr << "novac not found: " << novac.string() << " (run from the repository root)\n"; return 1; }
    if (run_shell(std::string("clang --version > ") + NULL_DEV + " 2>&1") != 0) {
        std::cout << "test_examples: clang not found on PATH, end-to-end examples skipped.\n";
        return 0;
    }
    std::error_code ec;
    fs::create_directories(work, ec);

    std::vector<fs::path> goldens;
    for (auto& e : fs::directory_iterator(tests / "expected"))
        if (e.path().extension() == ".out") goldens.push_back(e.path());
    std::sort(goldens.begin(), goldens.end());

    int failed = 0;
    for (const auto& golden : goldens) {
        std::string name = golden.stem().string();
        fs::path exe = work / (name + EXE), actual = work / (name + ".actual");
        fs::path in = tests / "expected" / (name + ".in"), exit_file = tests / "expected" / (name + ".exit");

        std::string build = "\"" + native(novac) + "\" build " + (tests / (name + ".nvpp")).generic_string() +
                            " -o " + exe.generic_string() + " > " + NULL_DEV + " 2>&1";
        if (run_shell(build) != 0) { std::cout << "FAIL " << name << ": novac could not build it\n"; ++failed; continue; }

        std::string run = "\"" + native(exe) + "\"";
        if (fs::exists(in)) run += " < \"" + native(in) + "\"";
        run += " > \"" + native(actual) + "\" 2>&1";
        int code = run_shell(run);

        int want_code = 0;
        if (fs::exists(exit_file)) std::istringstream(read_text(exit_file)) >> want_code;
        std::string want = read_text(golden), got = read_text(actual);
        if (got == want && code == want_code) { std::cout << "ok   " << name << "\n"; continue; }

        ++failed;
        std::cout << "FAIL " << name << " (exit " << code << ", expected " << want_code << ")\n";
        std::istringstream a(want), b(got);
        std::string la, lb;
        for (int n = 1;; ++n) {
            bool ha = static_cast<bool>(std::getline(a, la)), hb = static_cast<bool>(std::getline(b, lb));
            if (!ha && !hb) break;
            if (!ha || !hb || la != lb) {
                std::cout << "  line " << n << ": expected [" << (ha ? la : "<end>") << "] got [" << (hb ? lb : "<end>") << "]\n";
                break;
            }
        }
    }
    fs::remove("io_test.txt", ec); // written by the io example
    if (failed) { std::cout << "FAILED: " << failed << " example(s)\n"; return 1; }
    std::cout << "All " << goldens.size() << " examples passed.\n";
    return 0;
}
