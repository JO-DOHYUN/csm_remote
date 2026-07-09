import re
import math
import json
import argparse
from pathlib import Path
from typing import Optional

import pandas as pd

HEX_RE = re.compile(r"0x[0-9A-Fa-f]+")
WS_RE = re.compile(r"\s+")


def is_nan(x):
    return x is None or (isinstance(x, float) and math.isnan(x))


def s(x):
    return "" if is_nan(x) else str(x).strip()


def normalize_name(x) -> str:
    txt = s(x)
    if not txt:
        return ""
    txt = txt.replace("\r", " ").replace("\n", " ").replace("\t", " ")
    txt = WS_RE.sub(" ", txt).strip()
    txt = re.sub(r"\s*_\s*", "_", txt)
    txt = WS_RE.sub(" ", txt).strip()
    return txt


def to_rule_id(cid: int, id_as_decimal: bool):
    return int(cid) if id_as_decimal else f"0x{cid:X}"


def id_to_int(v) -> int:
    if isinstance(v, int):
        return v
    if isinstance(v, str) and v.lower().startswith("0x"):
        return int(v, 16)
    return int(v)


def extract_ids(cell: str):
    """
    Identifier 셀에서 0x.. ID 추출
    - "0x210\n…\n0x217" 같은 범위는 확장
    - 같은 셀 안에 여러 개가 있으면 중복 제거 후 순서 유지
    """
    txt = s(cell)
    if not txt:
        return []

    hexes = [int(h, 16) for h in HEX_RE.findall(txt)]
    if not hexes:
        return []

    if ("…" in txt or "..." in txt) and len(hexes) >= 2:
        a, b = hexes[0], hexes[-1]
        if a <= b:
            return list(range(a, b + 1))
        return list(range(b, a + 1))

    out, seen = [], set()
    for v in hexes:
        if v not in seen:
            seen.add(v)
            out.append(v)
    return out


def find_header_row(excel: Path, sheet: str, max_scan_rows: int = 20) -> int:
    """
    같은 양식의 엑셀이라면 상단 설명행 수가 조금 달라도 잡을 수 있게
    앞쪽 몇 줄을 스캔해서 Name / Identifier / period 헤더가 있는 줄을 찾는다.
    못 찾으면 기존 값 5를 기본으로 사용.
    """
    raw = pd.read_excel(excel, sheet_name=sheet, header=None, nrows=max_scan_rows)
    for ridx in range(len(raw)):
        vals = [s(v).lower() for v in raw.iloc[ridx].tolist()[:8]]
        joined = " | ".join(vals)
        has_name = any(v == "name" for v in vals)
        has_id = any("identifier" in v for v in vals)
        has_period = any("period" in v for v in vals)
        if has_name and has_id and has_period:
            return ridx
    return 5


def build_rules(
    excel: Path,
    sheet: str,
    *,
    bus: Optional[int],
    id_as_decimal: bool,
    ttl_warn_mult: float,
    ttl_err_mult: float,
    period_warn_pct: float,
    period_err_pct: float,
    header_row: Optional[int] = None,
):
    hdr = find_header_row(excel, sheet) if header_row is None else int(header_row)
    df = pd.read_excel(excel, sheet_name=sheet, header=hdr)

    # 반복 헤더 1줄 제거("Name"이 데이터로 들어오는 경우)
    if len(df) > 0 and s(df.iloc[0, 0]).lower() == "name":
        df = df.iloc[1:].reset_index(drop=True)

    COL_NAME = 0   # Name
    COL_ID = 1     # Identifier [Hex]
    COL_CYCLE = 2  # period(ms)

    rules = []
    cur_name = ""

    for _, row in df.iterrows():
        if not is_nan(row.iloc[COL_NAME]):
            cur_name = normalize_name(row.iloc[COL_NAME])

        ids = extract_ids(row.iloc[COL_ID])
        if not ids:
            continue

        exp_ms = None
        if not is_nan(row.iloc[COL_CYCLE]):
            try:
                exp_ms = float(row.iloc[COL_CYCLE])
            except Exception:
                exp_ms = None

        for cid in ids:
            rule = {"id": to_rule_id(cid, id_as_decimal)}

            if bus is not None:
                rule["bus"] = int(bus)

            if cur_name:
                rule["name_en"] = cur_name

            if exp_ms is not None:
                rule["expected_period_ms"] = exp_ms
                rule["ttl_warn_ms"] = exp_ms * ttl_warn_mult
                rule["ttl_err_ms"] = exp_ms * ttl_err_mult
                rule["period_err_warn_pct"] = period_warn_pct
                rule["period_err_err_pct"] = period_err_pct

            rules.append(rule)

    return {"rules": dedupe_and_sort_rules(rules)}


def dedupe_and_sort_rules(rules: list[dict]):
    uniq = {}
    for r in rules:
        key = (r.get("bus", None), r["id"])
        uniq[key] = r
    out = list(uniq.values())
    out.sort(key=lambda r: ((r.get("bus", -1) if r.get("bus", None) is not None else -1), id_to_int(r["id"])))
    return out


def merge_rule_objects(*objs: dict):
    merged = []
    for obj in objs:
        if obj and isinstance(obj, dict):
            merged.extend(obj.get("rules", []))
    return {"rules": dedupe_and_sort_rules(merged)}


def has_duplicate_ids_across_groups(sys_obj: dict, drv_obj: dict, with_bus: bool) -> list[str]:
    if with_bus:
        return []
    sys_ids = {r["id"] for r in sys_obj.get("rules", [])}
    drv_ids = {r["id"] for r in drv_obj.get("rules", [])}
    dup = sorted(sys_ids & drv_ids, key=id_to_int)
    return [str(x) for x in dup]


def make_out_path(outdir: Path, prefix: str, kind: str, suffix: str) -> Path:
    tail = f"_{suffix}" if suffix else ""
    return outdir / f"{prefix}_{kind}{tail}.json"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--excel", required=True, help="CAN DB 엑셀 경로")
    ap.add_argument("--outdir", default="./db_out", help="출력 폴더")
    ap.add_argument("--only", choices=["SYSTEM", "DRIVING", "MERGED", "ALL"], default="ALL")

    # 같은 양식의 다른 엑셀에도 재사용 가능하도록 시트명/출력명은 옵션화
    ap.add_argument("--system-sheet", default="CAN DB_LINE2(SYSTEM)", help="SYSTEM 시트명")
    ap.add_argument("--driving-sheet", default="CAN DB_LINE1(DRIVING)", help="DRIVING 시트명")
    ap.add_argument("--prefix", default="vms_rules", help="출력 파일 prefix")
    ap.add_argument("--suffix", default="R13", help="출력 파일 suffix. 비우면 접미사 없음")
    ap.add_argument("--header-row", type=int, default=None,
                    help="헤더 줄 0-based 인덱스 강제 지정. 생략 시 자동 탐색 후 실패하면 5 사용")

    # 병합 파일에서 ID 충돌 방지용으로 SYSTEM/DRIVING bus를 분리 지정 가능
    ap.add_argument("--system-bus", type=int, default=None, help="SYSTEM 규칙에 bus 넣기")
    ap.add_argument("--driving-bus", type=int, default=None, help="DRIVING 규칙에 bus 넣기")

    ap.add_argument("--id-dec", action="store_true",
                    help="id를 10진 정수로 출력(기본은 '0x520' 형태의 문자열)")

    ap.add_argument("--ttl-warn-mult", type=float, default=3.0, help="ttl_warn_ms = period * 이 값")
    ap.add_argument("--ttl-err-mult", type=float, default=10.0, help="ttl_err_ms = period * 이 값")
    ap.add_argument("--perr-warn", type=float, default=20.0, help="period_err_warn_pct 기본값")
    ap.add_argument("--perr-err", type=float, default=50.0, help="period_err_err_pct 기본값")

    args = ap.parse_args()

    excel = Path(args.excel).expanduser().resolve()
    outdir = Path(args.outdir).expanduser().resolve()
    outdir.mkdir(parents=True, exist_ok=True)

    sys_obj = None
    drv_obj = None

    need_system = args.only in ("SYSTEM", "MERGED", "ALL")
    need_driving = args.only in ("DRIVING", "MERGED", "ALL")

    if need_system:
        sys_obj = build_rules(
            excel,
            args.system_sheet,
            bus=args.system_bus,
            id_as_decimal=args.id_dec,
            ttl_warn_mult=args.ttl_warn_mult,
            ttl_err_mult=args.ttl_err_mult,
            period_warn_pct=args.perr_warn,
            period_err_pct=args.perr_err,
            header_row=args.header_row,
        )
        if args.only in ("SYSTEM", "ALL"):
            p = make_out_path(outdir, args.prefix, "SYSTEM", args.suffix)
            p.write_text(json.dumps(sys_obj, ensure_ascii=False, indent=2), encoding="utf-8")
            print("OK:", p)

    if need_driving:
        drv_obj = build_rules(
            excel,
            args.driving_sheet,
            bus=args.driving_bus,
            id_as_decimal=args.id_dec,
            ttl_warn_mult=args.ttl_warn_mult,
            ttl_err_mult=args.ttl_err_mult,
            period_warn_pct=args.perr_warn,
            period_err_pct=args.perr_err,
            header_row=args.header_row,
        )
        if args.only in ("DRIVING", "ALL"):
            p = make_out_path(outdir, args.prefix, "DRIVING", args.suffix)
            p.write_text(json.dumps(drv_obj, ensure_ascii=False, indent=2), encoding="utf-8")
            print("OK:", p)

    if args.only in ("MERGED", "ALL"):
        if sys_obj is None:
            sys_obj = build_rules(
                excel,
                args.system_sheet,
                bus=args.system_bus,
                id_as_decimal=args.id_dec,
                ttl_warn_mult=args.ttl_warn_mult,
                ttl_err_mult=args.ttl_err_mult,
                period_warn_pct=args.perr_warn,
                period_err_pct=args.perr_err,
                header_row=args.header_row,
            )
        if drv_obj is None:
            drv_obj = build_rules(
                excel,
                args.driving_sheet,
                bus=args.driving_bus,
                id_as_decimal=args.id_dec,
                ttl_warn_mult=args.ttl_warn_mult,
                ttl_err_mult=args.ttl_err_mult,
                period_warn_pct=args.perr_warn,
                period_err_pct=args.perr_err,
                header_row=args.header_row,
            )

        dups = has_duplicate_ids_across_groups(
            sys_obj,
            drv_obj,
            with_bus=(args.system_bus is not None or args.driving_bus is not None),
        )
        if dups:
            print("WARN: SYSTEM/DRIVING 사이 중복 ID가 있습니다. 병합 JSON에서 마지막 값으로 덮일 수 있습니다.")
            print("WARN: 중복 ID 예시:", ", ".join(dups[:20]))
            print("WARN: 필요하면 --system-bus / --driving-bus 로 bus를 분리하세요.")

        merged = merge_rule_objects(sys_obj, drv_obj)
        p = make_out_path(outdir, args.prefix, "MERGED", args.suffix)
        p.write_text(json.dumps(merged, ensure_ascii=False, indent=2), encoding="utf-8")
        print("OK:", p)


if __name__ == "__main__":
    main()
