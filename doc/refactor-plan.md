# Cmpl Refactor Plan — round 3 (instructions for Agent B)

**Status: ACTIVE — one instruction left.** Rounds 1–2 done and verified
(Stage B 88/88, self-build 122/122). Only the directory-count rule (≤9 .c per
directory) is still violated in `ir/` and `ir/dump/` — see R3-1. Everything
else is compliant. Each instruction is one logical unit (one commit).

## Completed (do not redo)

| Instruction | Commits | Result |
|---|---|---|
| R2-1 dedupe decl_build_func_def + split decl parsers | e53bb7a | ✅ both parsers ≤80, dedupe landed |
| R2-2 unify comma-rewrite → parse_init_expr_until | 5c3f0cc | ✅ one implementation, third copy deleted |
| R2-3 split lr1.c → lr1_cast.c + lr1_cast_apply.c | f0001bc | ✅ lr1.c ≤200 |
| R2-4 split ir_type.c → ir_type_ast.c + ir_type_struct.c | 96a458e | ✅ ir_type.c ≤200 |
| R2-5 split ir_dump_instr.c → instr_extra + instr_gep | a130a47 | ✅ |
| R2-6 split main.c → main_driver.c | 70f51da | ✅ main.c ≤200 |
| R2-7 remaining ≤200 splits (ll_declarator, dump_module, dump, builder_ops, builder, pp/inc) | 802821c..f051505 | ✅ all ≤200 (see §3 PASS list for the 7 borderline files) |

## R3-1 (P1) Fix directory-count violations in ir/ (11) and ir/dump/ (10)

- **Files:** move these files (update ir/CMakeLists.txt `add_library` paths,
  `target_include_directories`, scripts/build_self_linux.sh SOURCES,
  scripts/build_self.ps1 $sources):
  1. `git mv` `ir/ir_builder.c ir/ir_builder_block.c ir/ir_builder_cast.c
     ir/ir_builder_const.c ir/ir_builder_mem.c ir/ir_builder_ops.c` →
     `ir/builder/` (6 files)
  2. `git mv` `ir/ir_type.c ir/ir_type_ast.c ir/ir_type_layout.c
     ir/ir_type_struct.c` → `ir/type/` (4 files)
  3. `git mv` `ir/dump/ir_dump_instr.c ir/dump/ir_dump_instr_extra.c
     ir/dump/ir_dump_instr_gep.c` → `ir/dump/instr/` (3 files)
- **Resulting counts (all ≤9):** ir/ root = ir_gen_cuda.c only (+headers) ·
  ir/builder/ = 6 · ir/type/ = 4 · ir/dump/ = 7 · ir/dump/instr/ = 3 ·
  ir/gen/ = 9 · ir/gen/expr/ = 9 · ir/gen/init/ = 7.
- **Include changes (REQUIRED — the two self-build scripts only pass
  `-I./ir`, not `-I./ir/dump`):**
  - In the 3 moved `ir/dump/instr/*.c` files, change `#include "ir_dump.h"`
    → `#include "../ir_dump.h"` (same convention as `../ir_gen.h` used by
    ir/gen/expr/). `ir.h` / `ir_api.h` still resolve via `-Iir`.
  - `ir/builder/*.c` and `ir/type/*.c` keep `#include "ir.h"` /
    `#include "ir_builder.h"` / `#include "ir_type.h"` — these live in ir/
    root and resolve via `-Iir`; no change needed. If any moved file uses a
    same-subdir sibling header, use `../` accordingly.
  - Top-level `CMakeLists.txt` `include_directories`: add `ir/builder`,
    `ir/type`, `ir/dump/instr`. `ir/CMakeLists.txt`
    `target_include_directories(ir PUBLIC ...)`: add the same three dirs.
  - `scripts/build_self_linux.sh` + `scripts/build_self.ps1`: add
    `-I./ir/builder -I./ir/type -I./ir/dump/instr` next to the existing
    `-I./ir` (or rely on `../` includes — prefer the explicit -I for
    robustness).
- **Why:** `ir/` has 11 .c and `ir/dump/` has 10 — the only remaining
  rule violations; the rule mandates submodules when a directory exceeds 9.
- **Expected:** every directory ≤9 .c; zero behavior change; self-build still
  passes (sources list must match the new paths exactly — the build_self
  scripts enumerate files, so a missed path fails the link stage).
- **Verify:** `cmake --build build/cmake_try -j4` → Stage B 88/88 →
  `scripts/build_self_linux.sh` Passed 122/122 + link SUCCESS. Then re-run
  `scripts/dircheck.sh` → no output (no dir over 9).
- **Deps:** none.

## Verified state after R3-1 (expected final)

- Directory .c counts: all ≤9 ✅
- Files >200 lines: only the 7 borderline files below (≤218, "slightly over
  is acceptable", TODO-tagged) ✅
- Functions >80 lines: only the 4 PASS items below (table-init data, lookup
  table, big switch) ✅
- No duplicate code; K&R clean; Stage B 88/88; self-build 122/122 ✅

## Explicitly marked PASS — do not refactor

- lr1_table_init_goto (118) / lr1_table_init_reds (104): table-init data.
- token_kind_name (92, main_pp.c): designated-initializer lookup table.
- walk_launch_children (88, cuda/cuda_launch.c): big switch, trivial cases.
- ll_parse_decl (84, parser/ll/decl/ll_decl_dispatch.c): slightly over,
  acceptable.
- Borderline files at 205–218 lines: ir/gen/ir_gen_stmt_ctrl.c (218),
  ir-opt/ir_opt_dce.c (215), ir/gen/expr/ir_gen_binary.c (214),
  ir/gen/init/ir_gen_init_desig.c (209), ir/gen/expr/ir_gen_unary.c (208),
  tokenizer/lexer.c (205), pp/pp.c (205) — leave the existing
  TODO(refactor) markers in place.

## Verification protocol (run after EVERY instruction)

1. `cmake --build build/cmake_try -j4`
2. `bash scripts/stageB_check.sh ./build/cmake_try/cmpl` → expect PASS=88 FAIL=0
3. If ir/, parser/, ast-opt/, pp/, tokenizer/ touched:
   `bash scripts/build_self_linux.sh` → expect Passed 122/122 + link SUCCESS
4. Never delete a failing test; add a regression test first if behavior changes.

## Standing rules for B

- ≤200 lines/file, ≤80 lines/function (slightly over acceptable).
- Empty line between functional blocks; K&R braces.
- No duplicate code; big switch → extract heavy cases first, then mark pass.
- Upper-level rules may be temporarily violated during lower-level refactors
  ONLY with a `/* TODO(refactor): … */` comment.
- After adding/renaming/moving any .c file, update ALL of: the owning dir's
  CMakeLists.txt, scripts/build_self_linux.sh SOURCES,
  scripts/build_self.ps1 $sources, and (for subdir moves) the -I lists of the
  two self-build scripts.
- One logical unit per instruction/commit.
