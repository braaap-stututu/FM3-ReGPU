# config/

Phase 2: `fm3.toml` (XenonRecomp config for `default.xex` v0.0.0.8: setjmp/longjmp, function
boundaries, fptr list, SwitchToFiber; see FM3-QUIRKS.md Q1–Q7) and `fm3_switch_tables.toml`
(XenonAnalyse output, with the patched back-trace).

Code sections to cover (from `build\tools\xexdump.exe`): `.text` 821E0000+987BCC, `_TEXT`
82B67C00+39C, `.embsec_` 82B68000+4E18, `PAGELK` 82B6D000+984, `BINK` 82B6DA00+10728.
