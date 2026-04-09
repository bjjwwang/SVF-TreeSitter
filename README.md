# svf-tree-sitter

A Tree-sitter–based C frontend for [SVF](https://github.com/SVF-tools/SVF). It
parses C source via `tree-sitter-c`, builds SVFIR (the same IR consumed by
SVF's WPA / DDA / SABER / AE / MTA clients) and hands off to the unmodified
analysis engines. **No LLVM is linked** — only `libSvfCore`.

```
.c source ─► tree-sitter-c CST ─► TSIRBuilder ─► SVFIR ─► Andersen / WPA / …
```

## What works today

| Area | Status |
|---|---|
| `AddrStmt`, `CopyStmt`, `LoadStmt`, `StoreStmt` | ✅ |
| Locals, globals, function parameters | ✅ |
| `if` / `while` / `for` (skeletal ICFG) | ✅ |
| Direct calls — `CallPE`, `RetPE`, `CallICFGNode`, `RetICFGNode`, `CallCFGEdge`, `RetCFGEdge`, `CallGraph` direct edges | ✅ |
| Two-pass function prescan (forward references work) | ✅ |
| `return` flow into a per-function return ValVar | ✅ pointer-returning functions |
| `malloc` / `calloc` / `realloc` → heap obj | ✅ |
| **Const-offset GEP — Cat 1 single-level** `s.x`, `p->x`, `&s.x` | ✅ |
| **Const-offset GEP — Cat 2 nested** `s.a.b`, `p->next->data`, `s.inner.x`, `p->inner->x` | ✅ via `nodeStructTy` chain |
| **Const-offset GEP — Cat 3 array** `arr[0]`, `arr[i]`, `s.arr[i]` (SVF is array-insensitive → idx 0) | ✅ |
| **Const-offset GEP — Cat 4 pointer arith** `p + n`, `p - n`, `n + p` (collapsed to `&p[0]`) | ✅ |
| **Const-offset GEP — Cat 5 cast** `(T*)p` → `CopyStmt` + struct-tag propagation | ✅ |
| **Const-offset GEP — Cat 6 combinations** `items[i].field`, `items[i].p->x`, `(*pp)->field` | ✅ (chain works automatically) |
| Indirect calls (function pointers) | ⚠️ visit args, no `CallPE` (sound stub) |
| `vararg` calls | ⚠️ extras dropped, no `CallPE` |
| Verifier: gep type-correctness, CallPE/RetPE arity check | ❌ |
| Multi-file translation units | ✅ each file processed in turn |

The SVFIR built by this frontend is structurally different from clang+wpa's
output (no SSA, no PHIs) but **semantically equivalent for points-to**: the
test suite's L3 layer compares Andersen results against `clang -O0 -emit-llvm`
+ `wpa` and asserts our pts is a sound superset.

## Build

Prerequisite: SVF built somewhere local (we link `libSvfCore.so` only).

```bash
git clone --recursive https://github.com/bjjwwang/SVF-TreeSitter.git
cd SVF-TreeSitter
mkdir build && cd build
cmake .. \
  -DSVF_DIR=/path/to/SVF \
  -DSVF_INCLUDE_DIR=/path/to/SVF/svf/include \
  -DSVF_CORE_LIB=/path/to/SVF/Release-build/lib/libSvfCore.so
make -j$(nproc)
```

If `SVF` exposes a `find_package(SVF)` config, `-DSVF_DIR=/path/to/SVF/Release-build`
alone is enough.

The build produces `build/ts-svf` (the CLI) and `build/libsvf-ts-core.a`
(reusable library).

## Run

```bash
export LD_LIBRARY_PATH=/path/to/SVF/Release-build/lib:/path/to/SVF/z3.obj/bin

# Verify SVFIR is structurally well-formed
./build/ts-svf --verify tests/basic/01_addr.c

# Run Andersen and dump points-to sets
./build/ts-svf --ander --dump-pts tests/inter/61_call.c

# Both
./build/ts-svf --verify --ander --dump-pts tests/struct/14_chain_arrow.c
```

Sample dump format (one PAG node per line, post-Andersen):

```
PTS p #6 -> {a.addr#11 }
PTS q.addr #16 -> {a.addr#11 }
PTS q #17 -> {q.addr#16 }
```

`GepObjVar`s are nameless inside SVFIR; the dumper synthesises a name from
their parent base object so the L3 normaliser can match them against
clang+wpa output.

## Tests

```bash
cd build
ctest --output-on-failure
```

There are three layers, all run by ctest:

- **L1 — structural verifier** (`SVFIRVerifier`). Asserts every node id
  resolves, every edge has live endpoints, every Addr/Copy/Load/Store dst is
  pointer-typed, every BaseObjVar (except specials) has an incoming AddrStmt.
  Required for every test.
- **L2 — inline ground truth**. Each test `.c` file may carry an
  `// EXPECTED:` block at the bottom listing exact pts and stmt counts:
  ```c
  // EXPECTED:
  // pts: a -> {a}
  // pts: p -> {a, p}
  // pts: q -> {a, q}
  // stmts: addr=5 copy=1 load=3 store=2 gep=0
  ```
  `scripts/check_expected.py` runs `ts-svf --dump-pts`, normalises through
  `extract_pts.py` (strips `.addr` suffixes, drops temporaries) and asserts
  every `pts:` line matches exactly + `stmts:` matches the verifier counts.
  Tests without an EXPECTED block are SKIPPED.
- **L3 — vs LLVM frontend soundness**. `scripts/run_compare.sh` builds the
  same `.c` with `clang -O0 -emit-llvm`, runs `wpa -ander -print-all-pts`,
  normalises both outputs through `extract_pts.py`, and asserts our pts is a
  sound *superset* of clang+wpa's on the variables they share. Requires
  `clang` and `wpa` from your SVF build (auto-detected; tests are disabled
  otherwise).

Current status: **60/60 passing**.

## Test layout

```
tests/
├── basic/           01–07: addr/copy/load/store/multi-level/global
├── control/         51 if, 52 loop
├── inter/           61 direct call (CallPE/RetPE)
├── struct/          11–12 single-level field; 13–14 nested + chain;
│                    15 global struct; 16 param struct
├── array/           21 const index; 22 var index; 23 array of struct
├── pointer_arith/   31 p + n
└── cast/            41 ptr-to-ptr cast then field
```

## Adding a new test

1. Drop a `.c` file in the relevant subdirectory.
2. Run `./build/ts-svf --verify --dump-pts your_test.c` and pipe through
   `python3 scripts/extract_pts.py ts` to see the normalised pts.
3. Append an `// EXPECTED:` block at the bottom matching the output:
   ```c
   // EXPECTED:
   // pts: <var> -> {<targets>}
   // ...
   // stmts: addr=N copy=N load=N store=N gep=N
   ```
4. `cd build && cmake .. && ctest` — the test is picked up automatically by
   the `file(GLOB_RECURSE)` in `tests/CMakeLists.txt`.

## Project layout

```
include/svf-ts/
├── TSFrontend.h        parser + builder driver (one entry per .c file)
├── TSIRBuilder.h       CST visitor — emits SVFIR
├── TSTypeBuilder.h     C type → SVFType + StInfo (flat fields)
├── TSSymbolTable.h     name → Symbol (val/obj/type/isParam/isArray/isFunction)
├── TSICFGBuilder.h     wraps SVF::ICFGBuilder; adds Call/Ret + edges
├── GepHandler.h        const / variant GEP emission
├── SimpleDataLayout.h  hard-coded x86-64 LP64 sizes
├── SVFIRVerifier.h     L1 structural checker
└── SvfShim.h           friend-by-name shims around SVFIR / ICFG private API
src/                    same files, .cpp
scripts/                extract_pts.py, check_expected.py, run_compare.sh,
                        validate_all.sh
tests/                  see above
third_party/            tree-sitter, tree-sitter-c (submodules)
```

## Design notes

- **Friend-by-name shim**. SVFIR/ICFG declare `friend class SVFIRBuilder;` /
  `friend class ICFGBuilder;`. The real implementations live in `svf-llvm`
  which we deliberately don't link. We re-declare those classes in
  `namespace SVF` inside `SvfShim.h` and use them as thin pass-throughs to
  SVFIR/ICFG's protected `add*` methods. Linking is safe because
  `libSvfCore` contains no symbols for those classes.

- **Parameters use alloca-style backing slots**. Each formal gets a bare
  ValVar (the CallPE target) AND a stack slot. Function entry emits
  `StoreStmt(formal → slot)`, and reads of the param go through Load. This
  matches clang's parameter ABI so the L3 self-loop pattern (`p -> {p, …}`)
  is preserved.

- **Field-name table**. SVFStructType doesn't carry source-level field
  names. We maintain `structFields[sty]` = vector of `FieldMeta {name,
  pointee, isPointer}` parallel to the StInfo's flat field list, populated
  while parsing the struct definition.

- **Struct type chain (`nodeStructTy`)**. A `NodeID → SVFStructType*` map
  threaded through identifier load, GEP results (direct struct fields),
  pointer-loaded values (pointer-to-struct fields), array-of-struct
  subscripts and cast results. This is what makes `s.a.b`, `p->next->data`,
  `items[i].p->field`, and `(struct B*)pa->field` all emit constant-offset
  GepStmts in a single chain rather than collapsing to variant GEPs.

- **Array decay**. `Symbol::isArray` flags array-typed identifiers; the
  visitor returns `sym->valId` directly (no Load) for them. Without this,
  `int *arr[4]; arr[0] = &a;` would Load through the array slot before the
  GEP, and Andersen would see no propagation.

- **Pointer-arith heuristic**. In our PAG model every local-variable read
  produces a pointer-typed ValVar (since locals have alloca slots), so a
  PAG-level `isPointer()` check on `i + 1` would falsely fire. The visitor
  instead checks the *AST* operand: identifier whose declared type is
  `ptrType`, or any field/subscript/cast/pointer expression.

## What's not done

- Indirect calls (function pointers) — currently sound stub, no CallPE/RetPE.
- Vararg formals beyond fixed-arity prefix.
- Verifier extensions: GepStmt type correctness, CallPE/RetPE arity match
  against the called function, ICFG reachability.
- C constructs not yet handled: `switch`, `goto`, `do-while`, compound
  literals, designated initializers, bit fields, `_Atomic`, `__attribute__`
  parsing.
- Type-system completeness: typedef chains beyond one level, `enum`
  underlying types, `_Alignof`, VLA bounds, pointer-to-function types.

Next planned work: indirect calls + verifier extensions. See the test suite
for the canonical examples of what each implemented category should produce.

## License

Same as upstream SVF.
