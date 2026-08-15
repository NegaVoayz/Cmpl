# Cmpl Refactor Plan — round 2 (instructions for Agent B)

**Status: ACTIVE.** Round 1 (56 commits) is done and verified (Stage B 88/88,
self-build 106/106). The following instructions complete the remaining work.
Execute them in order; each is one logical unit (one commit).

## R2-1 (P1) Dedupe declarator/FUNC_DEF logic + split both decl parsers

- **Files:** parser/ll/decl/ll_decl.c, parser/ll/decl/ll_decl_struct.c, new
  parser/ll/decl/ll_decl_common.c; update parser/ll/CMakeLists.txt + both
  self-build scripts.
- **Change:** extract a shared helper
  `AST_Node* decl_build_func_def(LR1_Parser* p, Token* start, Type* full,
  String dname, int linkage, int is_constructor)` covering the identical
  TYPE_PTR-unwrap loop, TYPE_FUNC detection, pointer-chain rebuild into
  ret_type, and AST_FUNC_DEF node construction that is currently duplicated in
  `parse_var_list_decl` (ll_decl.c) and `parse_struct_union_decl`
  (ll_decl_struct.c). Call it from both parsers; then split each parser into
  ≤80-line parts (e.g. `parse_func_def_tail`, `parse_vardef_tail`). Finally,
  if ll_decl.c is still >200, move `parse_attribute` and `ll_parse_decl` into
  ll_decl_common.c (or a new ll_decl_attr.c).
- **Why:** two 132/142-line functions with ~40 duplicated lines; both files
  over the 200-line limit.
- **Expected:** both functions ≤80 (slightly over ok), both files ≤200, zero
  behavior change. Verify: Stage B 88/88 + self-build 106/106.
- **Deps:** none.

## R2-2 (P1) Unify the comma-rewrite workaround; delete third inline copy

- **Files:** parser/ll/decl/ll_decl.c, parser/ll/decl/ll_decl_init.c,
  parser/ll/decl/ll_decl_struct.c.
- **Change:** merge `parse_scalar_init` and `parse_init_element_expr` into one
  helper `parse_init_expr_until(LR1_Parser* p, TokenKind term)` (TOK_SEMI or
  TOK_RBRACE); replace the inline scan-replace-restore copy inside
  `parse_struct_union_decl` (ll_decl_struct.c) with it.
- **Why:** three copies of the same workaround; the rule says replace
  inelegant patches with proper logic and dedupe.
- **Expected:** one comma-rewrite implementation. Verify as R2-1.
- **Deps:** after R2-1 (same files).

## R2-3 (P1) Split parser/lr/lr1.c (481 lines)

- **Files:** parser/lr/lr1.c, new parser/lr/reduce/lr1_cast.c; update
  parser/lr/CMakeLists.txt + self-build scripts.
- **Change:** move `is_cast_start`, `parse_sizeof_type`,
  `skip_compound_literal`, `try_parse_cast`, `lr1_stop_at_comma`,
  `apply_pending_cast_at_reduce` into lr1_cast.c; keep
  `lr1_parse_expr_inner` + parser lifecycle in lr1.c (<200).
- **Why:** lr1.c is the largest remaining file.
- **Deps:** none.

## R2-4 (P1) Split ir/ir_type.c (423 lines)

- **Files:** ir/ir_type.c, new ir/ir_type_ast.c; update ir/CMakeLists.txt +
  self-build scripts.
- **Change:** move `ast_to_ir_type`, `ast_to_func_type`, `ast_to_struct_type`,
  `unsigned_of` into ir_type_ast.c (declare in ir_api.h or a new ir_type.h).
  ir_type.c keeps caches/singletons/constructors (<200).
- **Why:** ir_type.c is the second-largest remaining file.
- **Deps:** none.

## R2-5 (P1) Split ir/dump/ir_dump_instr.c (371 lines)

- **Files:** ir/dump/ir_dump_instr.c, new ir/dump/ir_dump_instr_extra.c;
  update ir/CMakeLists.txt + self-build scripts.
- **Change:** finish the existing TODO: extract GEP/BITCAST/TRUNC/ZEXT/SEXT/
  SITOFP/UITOFP/FPTOSI/FPTOUI/SELECT printers and the
  CALL/RET/BR/COND_BR/PHI/UNREACHABLE printers into the new file; keep
  alloca/load/store/arith/cmp in ir_dump_instr.c (<200).
- **Deps:** none.

## R2-6 (P1) Split main.c (287 lines)

- **Files:** main.c, new main_driver.c (add to CMPL_SOURCES in top-level
  CMakeLists.txt + both self-build scripts).
- **Change:** move run_cuda_pipeline / run_codegen_pipeline / run_ir_pipeline /
  run_ast_pipeline into main_driver.c; main.c keeps CmdOpts, parse_args, main.
- **Why:** main.c grew during round 1 because the pipeline helpers stayed in
  the same file.
- **Deps:** none.

## R2-7 (P1) Remaining ≤200 splittings (independent, any order)

- **Files + moves** (each atomic; update the owning dir CMakeLists.txt + both
  self-build scripts, and -I lists for new subdirs):
  - parser/ll/decl/ll_declarator.c (274): move ll_parse_params →
    decl/ll_declarator_params.c
  - ir/dump/ir_dump_module.c (266): extract dump_fnptr_declares +
    dump_extern_declares → ir/dump/ir_dump_declares.c
  - ir/dump/ir_dump.c (228): extract dump_type → ir/dump/ir_dump_type.c
  - ir/ir_builder_ops.c (236): extract ir_build_gep + cmp builders →
    ir/ir_builder_mem.c
  - ir/ir_builder.c (228): extract block mgmt (new_block/set_block/append) →
    ir/ir_builder_block.c if still over
  - pp/pp_include.c (251) + pp/pp_directive.c (239): create pp/inc/ subdir,
    move include-resolve path helpers → pp/inc/pp_include_paths.c and
    handle_define → pp/inc/pp_define.c (keeps pp/ root ≤9 .c)
- **Deps:** none.

## Explicitly marked PASS — do not refactor

- lr1_table_init_goto (118) / lr1_table_init_reds (104): table-init data.
- token_kind_name (92, main_pp.c): designated-initializer lookup table.
- walk_launch_children (88, cuda/cuda_launch.c): big switch, trivial cases.
- Files at 205–218 lines: ir_gen_stmt_ctrl.c, ir_opt_dce.c, ir_gen_binary.c,
  ir_gen_init_desig.c, ir_gen_unary.c, lexer.c, pp.c — "slightly over is
  acceptable"; leave the existing TODO(refactor) markers in place.
- ll_parse_decl (84, ll_decl.c): slightly over, acceptable.

## Verification protocol (run after EVERY instruction)

1. `cmake --build build/cmake_try -j4`
2. `bash scripts/stageB_check.sh ./build/cmake_try/cmpl` → expect PASS=88 FAIL=0
3. If ir/, parser/, ast-opt/, pp/, tokenizer/ touched:
   `bash scripts/build_self_linux.sh` → expect Passed 106/106 + link SUCCESS
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
