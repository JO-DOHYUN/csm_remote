@echo off
setlocal

set SCRIPT_DIR=%~dp0
set REPO_ROOT=%SCRIPT_DIR%..\..\..
set EXCEL=%REPO_ROOT%\reference\vehicle-db\HYM_HAMT2.0_CAN DB_R13_260206.xlsx
set PROFILE=%SCRIPT_DIR%profile_hamt_like_r13.json
set OUTDIR=%REPO_ROOT%\generated\can-db

py "%SCRIPT_DIR%vms_rules_from_excel_profiled_signals.py" ^
  --excel "%EXCEL%" ^
  --profile "%PROFILE%" ^
  --outdir "%OUTDIR%" ^
  --only ALL ^
  --system-bus 0 ^
  --driving-bus 1 ^
  --prefix vms_rules ^
  --suffix R13

pause
