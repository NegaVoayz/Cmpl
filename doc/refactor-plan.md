# Cmpl Refactor Plan (Agent A / architect)

Status: produced after baseline verification (cmake build OK, Stage B 87/87,
self-build 77/77). All line numbers refer to the current tree at plan time.

## 0. Verified baseline

- `cmake -S . -B build/cmake_try` + build: OK
- `scripts/stageB_check.sh <cmpl>`: PASS=87 FAIL=0 (cmpl = bootstrap or cmake_try binary)
- `scripts/build_self_linux.sh`: 77/77 sources pass IR gen + clang -c, link OK
- Tooling added (keep, they are useful for verification):
  - `scripts/count_lines.sh` — lines per source file
  - `scripts/scan_funcs.py` — functions >80 lines
  - `scripts/stageB_check.sh` — Stage-B IR validation over test/*.c
  - `scripts/stageB_check.sh <binary>` usage: pass the cmpl binary to test

## 1. Hard violations inventory

### 1.1 Directory .c-file count (rule: ≤9 per directory)

| Directory | .c count | Status |
|---|---|---|
| ir/ | 11 | VIOLATES |
| parser/lr/ | 11 | VIOLATES |
| ast-opt/ | 10 | VIOLATES |
| pp/, ir-opt/, parser/ll/ | 9 | at limit |
| others | ≤5 | OK |

### 1.2 Files >200 lines (18 files; two are extreme)

| File | Lines | Worst function(s) |
|---|---|---|
| ir/ir_gen_expr.c | **1606** | gen_expr 704 |
| ir/ir_gen.c | **1483** | ir_gen_module_ex 265; gen_const_init_list 236+119 |
| ir/ir_type.c | 579 | ast_to_ir_type 109 |
| parser/ll/ll_decl.c | 537 | parse_var_list_decl 209 |
| ir/ir_dump_func.c | 489 | ir_dump_module 229 |
| parser/lr/lr1.c | 446 | lr1_parse_expr_inner 177 |
| ir/ir_gen_stmt.c | 382 | gen_stmt 101; gen_stmt_switch 88 |
| ir/ir_dump_instr.c | 358 | dump_instr 341 |
| ir/ir_builder.c | 275 | — |
| ir/ir_builder_ops.c | 257 | — |
| parser/ll/ll_declarator.c | 248 | ll_parse_declarator 148 |
| main.c | 247 | main 228 |
| pp/pp_directive.c | 239 | — |
| pp/pp_include.c | 233 | include_resolve 136 |
| ir/ir_dump.c | 223 | dump_value 87 |
| dump_ast.c | 222 | — |
| ir-opt/ir_opt_dce.c | 215 | — |
| pp/pp.c | 205 | — |

### 1.3 Functions >80 lines (30 total; top offenders)

gen_expr 704 · dump_instr 341 · ir_gen_module_ex 265 · gen_const_init_list 236
· ir_dump_module 229 · main 228 · lr1_parse_expr_inner 177 · gen_binary_op 175
· ll_parse_declarator 148 · gen_store_ptr 146 · ll_decl_struct 142 ·
ir_gen_init_one 138 · include_resolve 136 · walk_launches 123 ·
ir_gen_function 119 · lr1_table_init_goto 118 · ast_to_ir_type 109 ·
lr1_table_init_reds 104 · expand_line 101 · gen_stmt 101 · read_operator 97 ·
ast_walk 91 · gen_stmt_switch 88 · dump_value 87 · is_straight_line 86 ·
opt_fold 85 · transform_call 84 · ll_parse_type_specs 84.
(token_kind_name 92 in main_pp.c is a designated-initializer lookup table —
data, not logic; mark PASS.)

### 1.4 Duplication / workarounds

1. **GenCtx triplicated with LAYOUT MISMATCH (latent bug).** ir_gen.c:27-37
   declares GenCtx *with* `struct SymSave* scope_top`; ir_gen_expr.c:13 declares
   it *without*; ir_gen_stmt.c:13 has it again. The struct is stack-allocated in
   ir_gen.c:474 and passed by pointer to gen_expr/gen_stmt. Different TUs see
   different struct sizes; it works only because the common prefix layout
   coincides. Adding any field to one copy silently corrupts the others.
   Fix: single shared header `ir/gen/ir_gen.h`.
2. **coerce_bool (ir_gen_expr.c:52) vs coerce_to_i1 (ir_gen_stmt.c:42)** —
   identical logic, duplicated.
3. **Const-init path mirrors runtime path.** ir_gen.c gen_const_cont_build /
   gen_const_advance / gen_const_cont_set vs ir_gen_expr.c desig_walk_slot /
   init_cont_advance / cont_walk_slot — comments literally say "mirrors … in
   ir_gen_expr.c". Continuation-cursor logic duplicated in const and runtime
   variants.
4. **skip_to_eol duplicated** — pp_directive.c:11 and pp_if.c:85 (identical).
5. **is_terminator name reused** in opt_dead.c (AST), ir_gen_stmt.c (IR opcode),
   lr1_table.c (TokenKind) — different domains; acceptable, but rename for
   clarity (optional, P2).
6. **parse_init_list comma-mutation hack** (ll_decl.c:163-183): scans ahead,
   temporarily rewrites a top-level comma token to TOK_SEMI so the LR parser
   treats it as terminator. A proper mechanism `p->stop_at_comma` already
   exists (lr1.c:252), used by ll_decl_agg.c for enums. Replace the hack with
   stop_at_comma if behavior matches (verify: diff_gcc corpus + init tests),
   else extract the hack into a named helper with a comment.
7. **ir_gen.c:532-539 typedef fallback workaround** — "if resolved type is i32
   but AST type is a named typedef … default to ptr — typedefs aren't resolved
   at parse time". Deliberate; keep, mark TODO (type-resolution ordering).

## 2. High-level plan by module

### ir/ (P0 — must restructure; the two monsters force it)
- Split into subdirectories so every dir ≤9 .c:
  - `ir/` root: ir.h, ir_api.h (headers) + ir_type.c, ir_builder.c,
    ir_builder_ops.c, ir_gen_cuda.c (≤5 .c after moves)
  - `ir/gen/`: ir_gen.c, ir_gen_expr.c + splits, ir_gen_stmt.c,
    ir_gen_resolve.c, ir_gen_const*.c (≤9 .c)
  - `ir/dump/`: ir_dump.c, ir_dump_func.c, ir_dump_instr.c, ir_dump_str.c (4)
- Extract `ir/gen/ir_gen.h`: GenCtx (one definition, with scope_top), SymSave,
  cross-file externs (gen_expr, gen_stmt, sym_*, ir_gen_init_one,
  ir_gen_zero_fill, gen_string_array_init, coerce_to_i1). Kill the extern
  soup at the top of ir_gen_expr.c / ir_gen_stmt.c.
- Split ir_gen_expr.c (1606) and ir_gen.c (1483) per instruction P0-4/P0-5.
- Every new file must be registered in: ir/CMakeLists.txt,
  scripts/build_self_linux.sh SOURCES, scripts/build_self.ps1 $sources.
  Subdir moves also require include-path updates in build_self_linux.sh /
  build_self.ps1 (quoted includes resolve relative to the source file's dir;
  `#include "ir.h"` from ir/gen/ needs -Iir; from ir/dump/ needs -Iir too —
  top-level CMakeLists already has -Iir; the two shell/ps1 self-build scripts
  only have -I./include -I./base -I. and must gain -Iir etc.).
  Windows build_self.ps1 cannot be executed on this Linux box: update the
  paths textually, verify via cmake + build_self_linux.sh.

### parser/lr/ (P1 file split, P2 dir count)
- lr1.c (446): extract cast-detection, sizeof(, and compound-literal skipping
  out of lr1_parse_expr_inner into helpers.
- Directory count 11→≤9: move table files into parser/lr/table/
  (lr1_table.c, lr1_table_acts.c, lr1_table_goto.c, lr1_table_reds.c) and
  reduce files into parser/lr/reduce/ (lr1_reduce*.c) — or merge
  lr1_reduce_passthrough.c (27) into lr1_reduce.c (168→195, still ≤200).
  Update parser/lr/CMakeLists.txt + both self-build scripts.

### parser/ll/ (P1)
- ll_decl.c (537): extract init-list/designator parsing into
  parser/ll/ll_decl_init.c; split parse_var_list_decl (209).
- ll_declarator.c (248): split ll_parse_declarator (148) into
  pointer/array/function layers.

### ast-opt/ (P2 dir count; file sizes OK)
- 10 .c → ≤9: move fold files (opt_fold.c, opt_fold_walk.c, opt_fold_try.c)
  into ast-opt/fold/ and propagate files (opt_propagate*.c) into
  ast-opt/propagate/; update ast-opt/CMakeLists.txt + self-build scripts.
- opt_fold (85) and ast_walk (91) — extract heavy cases per the big-switch
  rule.

### pp/ (P1)
- pp_include.c (233): include_resolve (136) → extract per-strategy helpers.
- pp_directive.c (239) + pp_if.c: dedupe skip_to_eol into a shared helper
  (pp_line.c or new pp_util.c); keep ≤9 .c in pp/ (at limit — merge into
  existing files rather than adding new ones, or use a subdir).
- pp.c (205) and pp_eval.c (195), pp_if.c (194): acceptable/slightly over;
  trim only if the dedupe work lands naturally.

### ir-opt/ (P2)
- ir_opt_dce.c 215 (slightly over): move uses_reserve/build_use_lists into a
  small ir_opt_uses.c only if ir-opt/ count permits (9 at limit — prefer
  merging into ir_opt_dce.c after trimming, or leave with a TODO).
- is_straight_line (86) in ir_opt_inline.c: extract instruction classification.

### main.c + dump_ast.c (P1)
- main (228): extract parse_args + one function per pipeline path
  (run_cuda, run_codegen, run_ir, run_ast) into main.c helpers or a
  drivers/ module; main.c → ≤200.
- dump_ast.c (222): split dump_expr/dump_stmt/dump_decl into
  dump_ast_stmt.c / dump_ast_decl.c or accept 222 (slightly over).

### tokenizer/ (P2)
- read_operator (97) in lexer.c: split by operator class.

### cuda/, vulkan/ (P2)
- walk_launches (123) in cuda_launch.c: extract per-node-kind walk.
- transform_call (84) in vk_mock.c: extract arg remapping.

## 3. Prioritized issues

### P0 (blocking / must fix first)
- P0-1 GenCtx layout-mismatch landmine → shared ir/gen/ir_gen.h (dedupes
  coerce_to_i1 too). [ir/]
- P0-2 ir/ subdirectory restructure (ir/gen/, ir/dump/) + all build manifests.
  [ir/, CMakeLists.txt, scripts/build_self_linux.sh, scripts/build_self.ps1]
- P0-3 Split ir/ir_gen_expr.c (1606) — gen_expr dispatcher + per-case files.
- P0-4 Split ir/ir_gen.c (1483) — resolve / module / const-init / function gen.

### P1 (major)
- P1-1 ir_type.c (579) split (ast_to_ir_type 109 → per-kind helpers).
- P1-2 parser/ll/ll_decl.c (537) split.
- P1-3 ir_dump_func.c (489) + ir_dump_module (229) split.
- P1-4 parser/lr/lr1.c (446) — lr1_parse_expr_inner (177) helper extraction.
- P1-5 ir_gen_stmt.c (382) — gen_stmt (101) + gen_stmt_switch (88) split.
- P1-6 ir_dump_instr.c (358) — dump_instr (341) big-switch → per-family printers.
- P1-7 ir_builder.c (275) + ir_builder_ops.c (257) — split const/cast builders.
- P1-8 main.c (247) — main (228) → parse_args + pipeline functions.
- P1-9 pp_include.c (233) — include_resolve (136) strategy helpers.
- P1-10 pp_directive.c (239) — skip_to_eol dedupe; handle_directive split.
- P1-11 ir_dump.c (223) — dump_value (87) per-kind extraction.
- P1-12 ll_declarator.c (248) — ll_parse_declarator (148) layer split.

### P2 (minor)
- P2-1 parser/lr dir count (11) and ast-opt dir count (10) → subdir moves.
- P2-2 parse_init_list comma-mutation hack → stop_at_comma (verify first).
- P2-3 dump_ast.c (222), ir_opt_dce.c (215), pp.c (205): slightly over;
  trim opportunistically, else leave with TODO.
- P2-4 read_operator (97), expand_line (101), walk_launches (123),
  transform_call (84), is_straight_line (86), opt_fold (85), ast_walk (91),
  ll_parse_type_specs (84), gen_stmt_switch (88), dump_value (87) — extract
  heavy cases per big-switch rule.
- P2-5 gen_const_* vs desig_walk_slot/init_cont_advance mirror — merge only
  if safe; else TODO comment.
- P2-6 rename duplicated is_terminator (optional clarity).

## 4. Verification protocol (run after EVERY instruction)

1. `cmake --build build/cmake_try -j4` (or a fresh refactor build dir)
2. `bash scripts/stageB_check.sh ./build/cmake_try/cmpl` → expect PASS=87 FAIL=0
3. If ir/, parser/, ast-opt/, pp/, tokenizer/ touched:
   `bash scripts/build_self_linux.sh` → expect Passed 77/77 + link SUCCESS
   (self-hosting catches silent IR/parser regressions; this is the real test)
4. Never delete a failing test; if behavior changes, add a test in test/ first.

## 5. Standing rules for B (from CLAUDE.md + refactor contract)

- ≤200 lines/file, ≤80 lines/function (slightly over acceptable).
- Empty line between functional blocks (allocation vs computation).
- K&R braces.
- No duplicate code — reuse the shared header, do not re-extern.
- Big switch: extract heavy cases into functions first, then mark pass.
- During a lower-level refactor, upper-level rules may be temporarily violated
  ONLY with a `/* TODO(refactor): … */` comment.
- Refactor at structure level: do not strip blank lines/comments unless
  redundant.
- Replace inelegant patches/workarounds with proper logic (see 1.4).
- After adding/renaming/moving any .c file, update ALL of:
  the owning dir's CMakeLists.txt, scripts/build_self_linux.sh SOURCES,
  scripts/build_self.ps1 $sources, and (for subdir moves) the -I lists of the
  two self-build scripts.
- Do not fix multiple issues in one commit/instruction unless explicitly told.
