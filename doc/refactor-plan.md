# Cmpl Refactor Plan — refactor-status doc

Tracks the standing refactor rules and the explicit PASS exemptions
(files/functions deliberately left over the size gates). Recreated after
`58de37e`; see git history for round 1–3 instruction records.

## Standing rules

- ≤200 lines/file, ≤80 lines/function (slightly over acceptable).
- Empty line between functional blocks; K&R braces.
- No duplicate code; big switch → extract heavy cases first, then mark pass.
- Upper-level rules may be temporarily violated during lower-level refactors
  ONLY with a `/* TODO(refactor): … */` comment.
- After adding/renaming/moving any .c file, update ALL of: the owning dir's
  CMakeLists.txt, scripts/build_self_linux.sh SOURCES,
  scripts/build_self.ps1 $sources, and (for subdir moves) the -I lists of
  the two self-build scripts.
- One logical unit per instruction/commit.

## Explicitly marked PASS — do not refactor

**Functions over 80 lines** (all data tables — splitting would scatter
unrelated rows):

- `lr1_table_init_goto` (118, parser/lr/table/lr1_table_goto.c): table-init
  data.
- `lr1_table_init_reds` (104, parser/lr/table/lr1_table_reds.c): table-init
  data.
- `token_kind_name` (101, main_pp.c): designated-initializer lookup table.

(`walk_launch_children` — formerly PASS at 88, gpu/gpu_launch.c — was split
under 80 in B-21 (`2fb8636`); no longer exempt.)

**Files at 201–236 lines** ("slightly over is acceptable"; the B-29 pass
band):

- ir/gen/sa/ice_eval.c (233) — B-29 case extraction, all functions ≤74
- ir/gen/sa/ice_type.c (228) — B-29 case extraction, all functions ≤60
- parser/ll/decl/ll_decl.c (218)
- ir/gen/ir_gen_stmt_ctrl.c (218) — `TODO(refactor)` marker kept
- ir/gen/expr/ir_gen_binary.c (218) — `TODO(refactor)` marker kept
- tokenizer/lexer.c (216)
- ir-opt/ir_opt_const.c (213)
- parser/lr/lr1.c (212)
- ir/type/ir_type_layout.c (210)
- parser/ll/ll_type.c (206)
- parser/lr/lr1_cast.c (204)
- ir-opt/ir_opt_simplify.c (204)
- ir/gen/resolve/ir_gen_resolve_struct.c (201)
- ir/gen/expr/lval/ir_gen_member.c (201)

## Status (B-29 complete)

- Functions >80 lines: 3 — all PASS-marked data tables above.
- Files >200 lines: 14 — all ≤236, PASS-marked above.
- Directory .c counts: all ≤9 (`scripts/dircheck.sh`).
- Stage A self-build + Stage B/C test suite: green (`scripts/run_tests.sh`).
