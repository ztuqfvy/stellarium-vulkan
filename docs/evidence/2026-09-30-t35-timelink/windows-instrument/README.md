# W-T35 batch — instrument defect, diagnosis and fix

This directory is **not** product evidence. It records a defect in the *measuring
instrument* (the Windows-side batch script `tools/windows/wt35-suites.ps1`) that
made the first W-T35 batch report a product regression that did not exist, and
the three-way closure that proves the fix.

The product-side readings from that same batch were all green and are archived
under `../windows/`. This directory exists because "the harness said FAIL" and
"the product is broken" are two different claims, and confusing them costs a
development round.

---

## 1. Symptom (first W-T35 batch, 2026-09-30 12:27–12:41 +08:00)

Verbatim from `C:\temp\t35w-suites\SUMMARY.txt`:

```
  TOOLBAR run1: rc=0 judge=12/12|PASS red=[] tb09found=1(>=1) covered=1(>=1) -> pos-pass
  TOOLBAR run2: rc=0 judge=|PASS      red=[] tb09found=1(>=1) covered=1(>=1) -> BAD
  TOOLBAR run3: rc=0 judge=/|PASS     red=[] tb09found=1(>=1) covered=1(>=1) -> BAD
  TOOLBAR summary: pos-pass=1 env-skip=0 bad=2 of 3
  TIMELINK run1: rc=0 judge=|PASS  red=[] leak=0(0) restore=1(>=1) gate=0 recomputedTL01=True recomputedTL07=True -> BAD
  TIMELINK run2: rc=0 judge=/|PASS red=[] ... -> BAD
  TIMELINK run3: rc=0 judge=|PASS  red=[] ... -> BAD
  TIMELINK summary: pos-pass=0 env-skip=0 bad=3 of 3 ; window-gate hits=0 (reading only)
FATAL TIMELINKCHECK never produced a full 7/7 run -- positive case not exercised
  TOOLBAR-NEG-A run1: rc=10 judge=/|FAIL red=[TB-07,TB-09,TB-12] expect=[TB-07,TB-09,TB-12] -> BAD
  ...
FATAL 13 expectation check(s) failed
```

Note what the ruined field is and what it is not:

| field | run1 | run2 | run3 | verdict |
|---|---|---|---|---|
| `judge=` (count + verdict) | `12/12\|PASS` | `\|PASS` | `/\|PASS` | **broken** |
| `rc=` | 0 | 0 | 0 | correct |
| `red=[]` (red criterion ids) | `[]` | `[]` | `[]` | correct |
| `tb09found` / `covered` | 1 / 1 | 1 / 1 | 1 / 1 | correct |
| `recomputedTL01` / `recomputedTL07` | True | True | True | correct |
| 7 negative controls, red-id sets | exact | exact | exact | correct |
| both probes | OK | OK | OK | correct |
| INTERACTCHECK 18/18 | pass | pass | pass | correct |

Only the one field that depends on a **regex built from a runtime variable** went
bad, and it went bad starting with the **second** call in the script. That
asymmetry is the whole clue.

## 2. Diagnosis path

Followed the standing rule: when an instrument reports a negative, first give the
instrument a diagnostic — do not go looking for a product defect.

1. **Pull the real bytes back.** The `.out.txt` files on the Windows box were
   correct: the criterion line was `…: 判据 12/12  VERDICT=PASS` with proper
   UTF-8 bytes (`E5 88 A4 E6 8D AE` for the count word), CRLF intact. So the
   product log was never wrong. The defect is downstream of the log.
2. **Print what the parser actually sees, on the target PowerShell.** A throwaway
   `judge-diag.ps1` was scp'd over and run with **Windows PowerShell 5.1** (not
   pwsh 7) — it dumped, for four different constructions of the count word, the
   runtime `type` / `len` / code points, and then how many times each candidate
   regex matched. Result:
   ```
   A plus       type=String len=2 cps=[21028,25454] value=[判据]
   A plus       hits=1 first=[12/12 verdict=PASS]
   ```
   i.e. the construction works and the regex works. Four constructions, four
   correct. So the fault was neither the literal nor the pattern.
3. **Therefore: the token is not what it was.** The regex was assembled as
   `"^" + $TagName + ":\s*" + $JUDGE + "\s*([0-9]+)/([0-9]+)…"`, and the caller
   read `$judge = Get-Judge …`.

## 3. Root cause

**PowerShell variable names are case-insensitive.** `$JUDGE` (the code-point
token) and `$judge` (the caller's result) are *the same variable*.

- Call 1: `$JUDGE` is the count word, the pattern is correct, `12/12|PASS`.
- Call 1 returns and assigns `12/12|PASS` back into the same variable.
- Call 2: `$JUDGE` is now `"12/12|PASS"`, so the pattern becomes
  ```
  ^TOOLBARCHECK:\s*12/12|PASS\s*([0-9]+)/([0-9]+)…
  ```
  — an **alternation whose right branch is unanchored**, so it matches nonsense
  and both captures come back `$null`. `"$jm|$vd"` then renders as `|PASS`
  (both `$null` → empty string), and once the alternation has bitten once it can
  even render `/|PASS`.

This is the **same trap already recorded for W-T31** (`$ok` overwriting `$OK`,
see the header of `wt35-suites.ps1`). It was fixed there for the check-mark
character and *not* generalised to the other non-ASCII tokens, so it came back
through a different door. That is the actual lesson: the fix in W-T31 was
instance-level, not class-level.

## 4. Fix — two independent measures, on purpose

1. **The token is gone.** `Get-Judge` no longer knows the count word at all; it
   skips whatever single non-space token sits between the colon and the count:
   ```
   $reJ = "^" + $TagName + ":\s+[^\s]+\s+([0-9]+)/([0-9]+)(\s+VERDICT=([A-Z]+))?"
   ```
   Now every anchor in the matcher is ASCII, so no variable name can collide
   with anything the matcher uses.
2. **The caller's result variable is renamed** `$judgeTxt` (all four call sites),
   which cannot collide with any token even if one is reintroduced later.

`\s+[^\s]+\s+` is deliberately the generic form rather than a literal: it also
survives a future change of the count word's wording or of the separator width.

## 5. Three-way closure (why this is enough)

| # | claim | evidence |
|---|---|---|
| a | the **old** instrument really does fail this way | `negctl-judge.ps1` rebuilds the old code verbatim (token + `$judge` caller) and prints `12/12\|PASS`, `\|PASS`, `/\|PASS` — **byte-identical in shape to the Windows SUMMARY**, reproduced on macOS/pwsh 7.6.6 |
| b | the **new** instrument parses every real log correctly | `test.ps1`: **97/97 PASS**, including all 5 macOS runs of each check, all 7 negative controls, both probes, the neighbour suites, and the negative-input section (truncated log → `\|`; injected `%.4f` leak → detected) |
| c | the new **regression assertions are load-bearing** | `test.ps1` grew a `REPEATED CALLS` section (call #1/#2/#3 identical; interleave a second tag; poison `$JuDgE`/`$JUDGE` with junk and re-read). (a) proves those assertions *would* have gone red on the old code — without (a) they would be decoration |

Claim (a) matters beyond bookkeeping: it shows the fault is **semantic**, not a
quirk of one PowerShell build. It reproduced on pwsh 7.6.6, the same script that
mangled itself on 5.1.

## 6. Files here

| file | what it is |
|---|---|
| `README.md` | this document |
| `test.ps1` | local truth test: the shipped parsers run against the **real** macOS logs in `../../t35-timelink/mac` and `../../t34-toolbar/mac`. Includes the `REPEATED CALLS` regression section |
| `defs.ps1` | **auto-extracted** from `tools/windows/wt35-suites.ps1` (token block + parser functions) — regenerated, never hand-edited, so the test always exercises the shipped code |
| `negctl-judge.ps1` | negative control: rebuilds the *old* instrument and shows it fails |
| `test.ps1.out.txt` | `ALL 97 CHECKS PASSED` |
| `negctl-judge.out.txt` | `NEGCTL OK -- the old instrument reproduces 3/3 signatures. => test.ps1's REPEATED CALLS section is load-bearing.` |
| `parse-check.txt` | pwsh 7 `Parser::ParseFile` on the shipped script: 0 errors, with the script's md5 |

**Regenerating `defs.ps1`** after any edit to `wt35-suites.ps1` — extract by *content
markers*, never by line number (line numbers drift, and they drift silently):

```sh
python3 - <<'PY'
src = open('tools/windows/wt35-suites.ps1', encoding='utf-8').read().split('\n')
a0 = next(i for i,l in enumerate(src) if l.startswith('$MARK_OK  ='))
a1 = next(i for i,l in enumerate(src) if l.startswith('$exe = Join-Path'))
b0 = next(i for i,l in enumerate(src) if l.startswith('function Run-Suite'))
b1 = next(i for i,l in enumerate(src) if i>b0 and l.startswith('# ===='))
open('docs/evidence/2026-09-30-t35-timelink/windows-instrument/defs.ps1','w',encoding='utf-8').write(
    '\n'.join(src[a0:a1] + [''] + src[b0:b1]).rstrip('\n') + '\n')
PY
```

Then re-run `test.ps1` **and** `negctl-judge.ps1` — a green `test.ps1` alone is not enough
(see §5 claim c).


Script hash for the fix: `tools/windows/wt35-suites.ps1`
`md5 = d53c59586f51ee22bfdb1b597441baa0` (superseded the defective
`d2800b278bed32a18daa9c33bd93ad95`; both `C:\temp\` and the `E:` checkout were
verified to hold the new bytes before the re-run).

## 7. Scope

Nothing in this directory is evidence about Stellarium. The re-run of the batch
with the fixed instrument is the product evidence, and it lives in `../windows/`.
