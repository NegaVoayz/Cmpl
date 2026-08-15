$ErrorActionPreference = "Continue"
# Use MinGW's clang/LLVM toolchain (has CRT libraries, unlike bare LLVM on Windows)
$env:PATH = "C:\MinGW\bin;C:\Program Files\LLVM\bin;$env:PATH"
# Force MinGW target so clang uses ld (not lld-link) and finds MinGW CRT libraries
$CLANG_FLAGS = "--target=x86_64-w64-mingw32"
Set-Location "$PSScriptRoot\.."

$sources = @(
    "main.c", "main_driver.c", "dump_ast.c", "dump_ast_decl.c",
    "base/arena.c", "base/hash.c",
    "tokenizer/parse.c", "tokenizer/lexer.c", "tokenizer/number.c", "tokenizer/ast.c",
    "pp/pp.c", "pp/pp_expand.c", "pp/pp_if.c", "pp/pp_eval.c", "pp/pp_cond.c",
    "pp/pp_macro.c", "pp/pp_directive.c", "pp/pp_include.c", "pp/pp_line.c",
    "parser/lr/lr1.c", "parser/lr/lr1_cast.c", "parser/lr/lr1_cast_apply.c", "parser/lr/lr1_shift.c",
    "parser/lr/reduce/lr1_reduce.c", "parser/lr/reduce/lr1_reduce_binary.c", "parser/lr/reduce/lr1_reduce_ctx.c",
    "parser/lr/reduce/lr1_reduce_postfix.c", "parser/lr/reduce/lr1_reduce_passthrough.c",
    "parser/lr/table/lr1_table.c", "parser/lr/table/lr1_table_acts.c", "parser/lr/table/lr1_table_goto.c", "parser/lr/table/lr1_table_reds.c",
    "parser/ll/ll.c", "parser/ll/ll_stmt.c", "parser/ll/ll_stmt_ctrl.c",
    "parser/ll/ll_stmt_ctrl_jump.c", "parser/ll/ll_type.c", "parser/ll/decl/ll_declarator.c", "parser/ll/decl/ll_declarator_params.c",
    "parser/ll/decl/ll_decl.c", "parser/ll/decl/ll_decl_common.c", "parser/ll/decl/ll_decl_dispatch.c", "parser/ll/decl/ll_decl_agg.c", "parser/ll/decl/ll_decl_struct.c", "parser/ll/decl/ll_decl_init.c",
    "parser/parse.c",
    "ast-opt/optimize.c", "ast-opt/ast_walk.c", "ast-opt/opt_enum.c", "ast-opt/opt_dead.c",
    "ast-opt/fold/opt_fold.c", "ast-opt/fold/opt_fold_walk.c", "ast-opt/fold/opt_fold_try.c",
    "ast-opt/propagate/opt_propagate.c", "ast-opt/propagate/opt_propagate_scan.c",
    "ast-opt/propagate/opt_propagate_replace.c",
    "ir/ir_type.c", "ir/ir_type_ast.c", "ir/ir_type_struct.c", "ir/ir_type_layout.c", "ir/ir_builder.c", "ir/ir_builder_const.c", "ir/ir_builder_cast.c", "ir/ir_builder_ops.c", "ir/ir_gen_cuda.c",
    "ir/gen/ir_gen.c", "ir/gen/ir_gen_module.c", "ir/gen/ir_gen_module_emit.c", "ir/gen/ir_gen_func.c", "ir/gen/ir_gen_resolve.c", "ir/gen/ir_gen_resolve_ast.c", "ir/gen/ir_gen_resolve_struct.c", "ir/gen/init/ir_gen_const.c", "ir/gen/init/ir_gen_init.c", "ir/gen/ir_gen_stmt.c", "ir/gen/ir_gen_stmt_ctrl.c", "ir/gen/expr/ir_gen_expr.c", "ir/gen/expr/ir_gen_logical.c", "ir/gen/expr/ir_gen_binary.c", "ir/gen/expr/ir_gen_unary.c", "ir/gen/expr/ir_gen_call.c", "ir/gen/expr/ir_gen_cast.c", "ir/gen/expr/ir_gen_ternary.c", "ir/gen/expr/ir_gen_lval.c", "ir/gen/expr/ir_gen_member.c", "ir/gen/init/ir_gen_init_desig.c", "ir/gen/init/ir_gen_const_desig.c", "ir/gen/init/ir_gen_const_cont.c", "ir/gen/init/ir_gen_const_list.c", "ir/gen/init/ir_gen_const_elem.c",
    "ir/dump/ir_dump.c", "ir/dump/ir_dump_type.c", "ir/dump/ir_dump_instr.c", "ir/dump/ir_dump_instr_extra.c", "ir/dump/ir_dump_instr_gep.c", "ir/dump/ir_dump_func.c", "ir/dump/ir_dump_module.c", "ir/dump/ir_dump_declares.c", "ir/dump/ir_dump_struct.c", "ir/dump/ir_dump_str.c",
    "cuda/cuda_qual.c", "cuda/cuda_split.c", "cuda/cuda_launch.c",
    "vulkan/vk_spirv.c", "vulkan/vk_spirv_collect.c", "vulkan/vk_spirv_emit.c",
    "vulkan/vk_spirv_func.c", "vulkan/vk_mock.c",
    "ir-opt/ir_opt.c", "ir-opt/ir_opt_mem2reg.c", "ir-opt/ir_opt_mem2reg_cfg.c",
    "ir-opt/ir_opt_mem2reg_rename.c", "ir-opt/ir_opt_dce.c", "ir-opt/ir_opt_const.c",
    "ir-opt/ir_opt_simplify.c", "ir-opt/ir_opt_gvn.c", "ir-opt/ir_opt_inline.c",
    "llvm-codegen/llvm_cg.c"
)

# ================================================================
# Stage 0: Find or build the cmpl binary to use for self-compilation
# ================================================================

# Prefer a previously self-built cmpl (has the enum/&&/crt fixes).
# Fall back to the bootstrap cmpl in build2/.
$CMPL = $null
if (Test-Path ".\build\self\cmpl_self.exe") {
    # Save a copy before cleaning build/self/
    Copy-Item ".\build\self\cmpl_self.exe" ".\build\cmpl_prev.exe" -Force
    $CMPL = ".\build\cmpl_prev.exe"
    Write-Host "Using previously self-built cmpl (saved to build/cmpl_prev.exe)"
} elseif (Test-Path ".\build2\cmpl.exe") {
    $CMPL = ".\build2\cmpl.exe"
    Write-Host "Using bootstrap: $CMPL"
}

if (-not $CMPL) {
    Write-Host "ERROR: No cmpl binary found. Run bootstrap build first."
    exit 1
}

# ================================================================
# Stage 1: Use cmpl to generate LLVM IR for all source files
# ================================================================

# Clean build dir
Remove-Item -Recurse -Force build/self -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force build/self | Out-Null

$failed = @()
$objFiles = @()

Write-Host "=== Stage 1: Generate LLVM IR with cmpl for all $($sources.Count) source files ==="
foreach ($src in $sources) {
    $base = $src -replace '[/\\]', '_'
    $base = [System.IO.Path]::GetFileNameWithoutExtension($base)
    $llFile = "build/self/$base.ll"
    $objFile = "build/self/$base.o"
    $objFiles += $objFile

    Write-Host "  $src -> $llFile"
    $result = & $CMPL -emit-llvm -I./include -I./base -I./ir -I. -o $llFile $src 2>&1
    if ($LASTEXITCODE -ne 0) {
        Write-Host "    FAIL (cmpl): $src"
        Write-Host $result
        $failed += $src
        continue
    }

    Write-Host "    -> $objFile"
    $clangResult = & clang $CLANG_FLAGS -c -o $objFile $llFile 2>&1
    if ($LASTEXITCODE -ne 0) {
        Write-Host "    FAIL (clang): $src"
        Write-Host $clangResult
        $failed += $src
    }
}

# Compile crt_shim.c directly with clang (not via cmpl) because it uses
# __attribute__((constructor)) which cmpl does not parse yet.
# It provides stdout/stderr/stdin symbols initialised from the UCRT.
Write-Host "  crt_shim.c (direct clang)"
& clang $CLANG_FLAGS -c -o build/self/crt_shim.o build/self_new/crt_shim.c 2>&1 | Out-Null
if ($LASTEXITCODE -ne 0) {
    Write-Host "    FAIL (clang direct): crt_shim.c"
    $failed += "crt_shim.c"
}
$objFiles += "build/self/crt_shim.o"

Write-Host ""
Write-Host "=== Results ==="
Write-Host "Passed: $($sources.Count - $failed.Count) / $($sources.Count)"
if ($failed.Count -gt 0) {
    Write-Host "Failed:"
    foreach ($f in $failed) { Write-Host "  $f" }
}

# ================================================================
# Stage 2: Link cmpl_self.exe
# ================================================================

Write-Host ""
Write-Host "=== Stage 2: Link cmpl_self.exe ==="
Write-Host "clang -o build/self/cmpl_self.exe build/self/*.o"
& clang $CLANG_FLAGS -o build/self/cmpl_self.exe build/self/*.o 2>&1
if ($LASTEXITCODE -ne 0) {
    Write-Host "LINK FAILED"
    exit 1
}
Write-Host "SUCCESS: cmpl_self.exe built!"

# ================================================================
# Stage 3: Self-compilation test
# ================================================================

Write-Host ""
Write-Host "=== Stage 3: Test cmpl_self.exe ==="
$testResult = & ./build/self/cmpl_self.exe -emit-llvm -I./include -I./base -I./ir -I. -o build/self/test_self.ll test/test.c 2>&1
if ($LASTEXITCODE -eq 0) {
    Write-Host "SELF-COMPILATION WORKS!"
} else {
    Write-Host "cmpl_self.exe failed (exit=$LASTEXITCODE)"
    Write-Host $testResult
    Remove-Item ".\build\cmpl_prev.exe" -ErrorAction SilentlyContinue
    exit 1
}

# Clean up the saved previous binary
Remove-Item ".\build\cmpl_prev.exe" -ErrorAction SilentlyContinue
