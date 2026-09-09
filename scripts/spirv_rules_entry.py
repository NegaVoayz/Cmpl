#!/usr/bin/env python3
"""spirv_rules_entry.py -- entry-point interface checks.

Split out of spirv_rules.py: every Workgroup variable must be listed in
each entry point's interface (VUID-StandaloneSpirv-04645), and an entry
point must be a void function.
"""

OP_TYPE_VOID = 19
STORAGE_WORKGROUP = 4


def check_workgroup_interface(entry_pts, var_storage, problems, path):
    """every Workgroup variable must be listed in each entry point"""
    wg = {vid for vid, sto in var_storage.items() if sto == STORAGE_WORKGROUP}

    for fid, interface in entry_pts:
        for vid in sorted(wg - set(interface)):
            problems.append(f"{path}: Workgroup variable {vid} is missing from "
                            f"entry point {fid}'s interface list")


def check_entry_points(entry_pts, func_ret, ty_of, problems, path):
    for fid, _interface in entry_pts:
        if fid not in func_ret:
            problems.append(f"{path}: entry point {fid} has no OpFunction")
            continue

        ret = ty_of.get(func_ret[fid])

        if not ret or ret[0] != OP_TYPE_VOID:
            problems.append(f"{path}: entry point {fid} must return void")
