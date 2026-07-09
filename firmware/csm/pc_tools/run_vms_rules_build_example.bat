@echo off
setlocal

REM 이 bat 파일과 같은 폴더에
REM 1) vms_rules_from_excel_profiled_signals.py
REM 2) profile_hamt_like_r13.json
REM 3) HYM_HAMT2.0_CAN DB_R13_260206.xlsx
REM 가 있다고 가정

set SCRIPT_DIR=%~dp0
set EXCEL=%SCRIPT_DIR%HYM_HAMT2.0_CAN DB_R13_260206.xlsx
set PROFILE=%SCRIPT_DIR%profile_hamt_like_r13.json
set OUTDIR=%SCRIPT_DIR%db_out

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
