# Reverse-engineering workspace

All artifacts needed to keep working on driver_lighthouse.so without redoing analysis.

| Path | What |
|---|---|
| `ghidra_proj/lh.gpr` | Headless Ghidra project (not in git — large). Functions recovered from `.eh_frame` (7.9k vs 735 from auto-analysis), PLT stubs named, bogus no-return flags fixed. |
| `ghidra_scripts/` | `EhFns` (create fns from FDEs), `FixNoRet` (name PLT, clear wrong noreturn, re-disassemble fallthroughs), `MapFns` (fn→strings/calls, vtables), `DumpAll`/`DumpRange` (decompile to files), `Reanalyze`. |
| `decomp_tracking/` | Decompiled C for 0x300000–0x3e8000 (vortex tracking core) **after** the noreturn fix. Authoritative. |
| `decomp_all_stale/` | Whole-binary decompile **before** the noreturn fix — functions that take a mutex are truncated. Use only for orientation. |
| `vtables.tsv` | vtable addr, offset-to-top, mangled class, slot→function. |
| `fn_strings.tsv`, `fn_calls.tsv` | Per-function referenced string literals / callees. |
| `plt.tsv` | PLT stub address → libc/libstdc++ symbol. |

Addresses are Ghidra addresses = ELF VA + 0x100000.

Re-run a range decompile:
```
/opt/ghidra/support/analyzeHeadless re/ghidra_proj lh -process driver_lighthouse.so -noanalysis -readOnly \
  -scriptPath re/ghidra_scripts -postScript DumpRange.java OUTDIR 300000 3e8000
```
Note: the user's GUI Ghidra (project `lhh`) also has the binary imported, but without these fixes.
