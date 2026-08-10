$ErrorActionPreference = "Continue"
$env:PATH = "C:\Program Files\LLVM\bin;$env:PATH"
Set-Location "D:\MyCodes\C\Cmpl"

# Use build2 which has a working cache (build/ was corrupted)
$CMPL = ".\build2\cmpl.exe"
Write-Host "Using: $CMPL"

# All source files that need to be compiled
$sources = @(
    "main.c", "dump_ast.c",
    "base/arena.c", "base/hash.c",
    "tokenizer/parse.c", "tokenizer/lexer.c", "tokenizer/number.c", "tokenizer/ast.c",
    "pp/pp.c", "pp/pp_expand.c", "pp/pp_if.c", "pp/pp_eval.c", "pp/pp_cond.c",
    "pp/pp_macro.c", "pp/pp_directive.c", "pp/pp_include.c", "pp/pp_line.c",
    "parser/lr/lr1.c", "parser/lr/lr1_shift.c", "parser/lr/lr1_table_goto.c",
    "parser/lr/lr1_table_acts.c", "parser/lr/lr1_table_reds.c", "parser/lr/lr1_reduce.c",
    "parser/lr/lr1_reduce_binary.c", "parser/lr/lr1_reduce_ctx.c",
    "parser/lr/lr1_reduce_postfix.c", "parser/lr/lr1_reduce_passthrough.c",
    "parser/lr/lr1_table.c",
    "parser/ll/ll.c", "parser/ll/ll_stmt.c", "parser/ll/ll_stmt_ctrl.c",
    "parser/ll/ll_stmt_ctrl_jump.c", "parser/ll/ll_type.c", "parser/ll/ll_declarator.c",
    "parser/ll/ll_decl.c", "parser/ll/ll_decl_agg.c", "parser/ll/ll_decl_struct.c",
    "parser/parse.c",
    "ast-opt/optimize.c", "ast-opt/ast_walk.c", "ast-opt/opt_fold.c",
    "ast-opt/opt_fold_walk.c", "ast-opt/opt_fold_try.c", "ast-opt/opt_propagate.c",
    "ast-opt/opt_propagate_scan.c", "ast-opt/opt_propagate_replace.c", "ast-opt/opt_dead.c",
    "ir/ir_type.c", "ir/ir_builder.c", "ir/ir_builder_ops.c", "ir/ir_gen.c",
    "ir/ir_gen_expr.c", "ir/ir_gen_stmt.c", "ir/ir_gen_cuda.c", "ir/ir_dump.c",
    "ir/ir_dump_instr.c", "ir/ir_dump_func.c", "ir/ir_dump_str.c",
    "cuda/cuda_qual.c", "cuda/cuda_split.c", "cuda/cuda_launch.c",
    "vulkan/vk_spirv.c", "vulkan/vk_spirv_collect.c", "vulkan/vk_spirv_emit.c",
    "vulkan/vk_spirv_func.c", "vulkan/vk_mock.c",
    "ir-opt/ir_opt.c", "ir-opt/ir_opt_mem2reg.c", "ir-opt/ir_opt_mem2reg_cfg.c",
    "ir-opt/ir_opt_mem2reg_rename.c", "ir-opt/ir_opt_dce.c", "ir-opt/ir_opt_const.c",
    "ir-opt/ir_opt_simplify.c", "ir-opt/ir_opt_gvn.c", "ir-opt/ir_opt_inline.c",
    "llvm-codegen/llvm_cg.c"
)

# Clean build dir
Remove-Item -Recurse -Force build/self -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force build/self | Out-Null

$failed = @()
$objFiles = @()

Write-Host "=== Step 1: Generate LLVM IR for all $($sources.Count) source files ==="
foreach ($src in $sources) {
    $base = [System.IO.Path]::GetFileNameWithoutExtension($src)
    $llFile = "build/self/$base.ll"
    $objFile = "build/self/$base.o"
    $objFiles += $objFile

    Write-Host "  $src -> $llFile"
    $result = & $CMPL -emit-llvm -I./include -I./base -I. -o $llFile $src 2>&1
    if ($LASTEXITCODE -ne 0) {
        Write-Host "    FAIL (cmpl): $src"
        Write-Host $result
        $failed += $src
        continue
    }

    Write-Host "    -> $objFile"
    $clangResult = & clang -c -o $objFile $llFile 2>&1
    if ($LASTEXITCODE -ne 0) {
        Write-Host "    FAIL (clang): $src"
        Write-Host $clangResult
        $failed += $src
    }
}

# Also compile crt_shim.c
Write-Host "  crt_shim.c"
& $CMPL -emit-llvm -I./include -I./base -I. -o build/self/crt_shim.ll build/self_new/crt_shim.c 2>&1 | Out-Null
& clang -c -o build/self/crt_shim.o build/self/crt_shim.ll 2>&1 | Out-Null
$objFiles += "build/self/crt_shim.o"

Write-Host ""
Write-Host "=== Results ==="
Write-Host "Passed: $($sources.Count - $failed.Count) / $($sources.Count)"
if ($failed.Count -gt 0) {
    Write-Host "Failed:"
    foreach ($f in $failed) { Write-Host "  $f" }
}

Write-Host ""
Write-Host "=== Step 2: Link cmpl_self.exe ==="
$objsStr = ($objFiles | ForEach-Object { "`"$_`"" }) -join " "
$linkCmd = "clang -o build/self/cmpl_self.exe $objsStr"
Write-Host $linkCmd
Invoke-Expression $linkCmd
if ($LASTEXITCODE -eq 0) {
    Write-Host "SUCCESS: cmpl_self.exe built!"
    Write-Host ""
    Write-Host "=== Step 3: Test cmpl_self.exe ==="
    & ./build/self/cmpl_self.exe -emit-llvm -I./include -I./base -I. -o build/self/test_self.ll test/test.c 2>&1
    if ($LASTEXITCODE -eq 0) {
        Write-Host "SELF-COMPILATION WORKS!"
    } else {
        Write-Host "cmpl_self.exe ran but may have issues (exit=$LASTEXITCODE)"
    }
} else {
    Write-Host "LINK FAILED"
}
