# ⚠️ THIS REPOSITORY IS NO LONGER MAINTAINED

Nova++ has evolved beyond its original transpiler architecture and is now being developed as a standalone programming language.

Development has moved to a new repository. All future updates, features, and improvements will be published there.

**→ [Check out the new Nova++ repository](https://github.com/migknightdev/Novapp)**

This repository is now archived and will remain available for historical reference.


# Nova++ v0.5.2-beta

This is a bugfix release: mixed-type `+` concatenation ("text " + number) now works everywhere in Nova++, not only inside `pf`.

---

## 🐛 Fixed

- **`"text " + number + " more text"` now compiles anywhere** — in assignments, `return`, and as arguments to any function call (e.g. `write_file`) — not only inside `pf`.

  Previously, a bare Nova++ string is a raw `const char*` in the generated C++. Mixing it with a number via `+` outside `pf` silently fell through to pointer arithmetic, and chaining a *second* literal (`"a" + n + "b"`) failed outright:
  ```
  error: no match for 'operator+' ... 'const char*' and 'const char [8]'
  ```
  This is now fixed at the root: any top-level `+` chain containing a string literal has its literal operands wrapped in `std::string(...)`, and the generated code carries `operator+(std::string, T)` overloads for arithmetic `T`. Pure numeric `+` (no string literal involved) is untouched and still compiles to ordinary addition.

## 📦 Full changelog

- **Fixed:** Mixed-type `+` concatenation outside `pf` (see above).
- **Added:** `operator+(std::string, T)` / `operator+(T, std::string)` overloads (arithmetic `T`) injected into the generated C++.

---

## 📥 Installation

### Linux / macOS

```bash
tar -xzvf novapp-0.5.2-beta.tar.gz
cd novapp-0.5.2-beta
chmod +x install.sh
./install.sh
```

No `sudo`? Install to your user directory instead:

```bash
PREFIX="$HOME/.local" ./install.sh
```

### Windows

**Option 1 — Pre-built installer (recommended):** download `novapp_setup.exe` from this release's assets and run it.

**Option 2 — Build from source:**

```bash
g++ -std=c++20 vcomp.cpp -o vcomp
```

### Verify

```bash
vcomp --version
# vcomp (Nova++) Compiler Version 0.5.2-beta
```

---

## 📚 Docs

- [`README.md`](./README.md) — overview, install/usage instructions
- [`Library.md`](./Library.md) — full language & standard library reference, including [Mixed-Type Concatenation](./Library.md#-mixed-type-concatenation)

**Full Changelog**: `v0.5.1-beta...v0.5.2-beta`
