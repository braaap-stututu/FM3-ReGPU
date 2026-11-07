# config/

- `fm3.toml`: XenonRecomp config for `default.xex` v0.0.0.8. Every entry says why it's there
  and how it was checked (quirk numbers refer to FM3-QUIRKS.md).
- `fm3_switch_tables.toml`: XenonAnalyse output (patched back-trace), 262 tables. Regenerate with
  `tools\recomp.ps1 -Prepare`, then review the git diff.

Pipeline: `& tools\recomp.ps1 [-Xex <default.xex>] [-Prepare]` → `out\ppc`, `out\recomp.log`,
`out\check.txt`. Tools: `build\tools\ppcdis.exe <xex> <hex addr> <count>` disassembles with
XenonRecomp's own decoder; `tools\fm3_config.py` finds helpers / exception words / setjmp
candidates; `tools\fm3_check.py` lists unresolved branches, traps, functions that run off their
end, and pointer targets that aren't function starts.

Code sections (from `build\tools\xexdump.exe`): `.text` 821E0000+987BCC, `_TEXT` 82B67C00+39C,
`.embsec_` 82B68000+4E18, `PAGELK` 82B6D000+984, `BINK` 82B6DA00+10728. Entry point 82529DD8.
