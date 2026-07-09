import re, math, json, argparse
from pathlib import Path
import pandas as pd

HEX_RE = re.compile(r"0x[0-9A-Fa-f]+")

def is_nan(x):
    return x is None or (isinstance(x, float) and math.isnan(x))

def s(x):
    return "" if is_nan(x) else str(x).strip()

def extract_ids(cell: str):
    """
    Identifier 셀에서 0x.. ID 추출
    - "0x210\n…\n0x217" 같은 범위는 확장
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
        else:
            return list(range(b, a + 1))
    # 중복 제거(순서 유지)
    out, seen = [], set()
    for v in hexes:
        if v not in seen:
            seen.add(v)
            out.append(v)
    return out

def build_rules(excel: Path, sheet: str,
                bus: int | None,
                id_as_decimal: bool,
                ttl_warn_mult: float,
                ttl_err_mult: float,
                period_warn_pct: float,
                period_err_pct: float):
    # 네 DB는 header=5가 맞음(6번째 줄이 컬럼 헤더)
    df = pd.read_excel(excel, sheet_name=sheet, header=5)

    # 반복 헤더 1줄 제거("Name"이 데이터로 들어오는 경우)
    if len(df) > 0 and s(df.iloc[0, 0]).lower() == "name":
        df = df.iloc[1:].reset_index(drop=True)

    COL_NAME  = 0  # Name
    COL_ID    = 1  # Identifier [Hex]
    COL_CYCLE = 2  # period(ms)

    rules = []
    cur_name = ""

    for _, row in df.iterrows():
        # 메시지명은 위에서 한 번 나오고 아래 시그널 라인에서는 빈 경우가 많아서 "최근값 유지"
        if not is_nan(row.iloc[COL_NAME]):
            cur_name = s(row.iloc[COL_NAME])

        ids = extract_ids(row.iloc[COL_ID])
        if not ids:
            continue

        # 기대주기
        exp_ms = None
        if not is_nan(row.iloc[COL_CYCLE]):
            try:
                exp_ms = float(row.iloc[COL_CYCLE])
            except:
                exp_ms = None

        for cid in ids:
            rid = int(cid) if id_as_decimal else f"0x{cid:X}"

            rule = {"id": rid}

            if bus is not None:
                rule["bus"] = int(bus)

            # 이름(기본: DB Name을 영문 이름으로)
            if cur_name:
                rule["name_en"] = cur_name
                # name_ko는 DB에 한글명이 없으니 기본은 생략(원하면 name_en 복사 가능)
                # rule["name_ko"] = cur_name

            # 기대주기 + TTL/주기오차 기준(원하면 옵션으로 조정)
            if exp_ms is not None:
                rule["expected_period_ms"] = exp_ms
                rule["ttl_warn_ms"] = exp_ms * ttl_warn_mult
                rule["ttl_err_ms"]  = exp_ms * ttl_err_mult
                rule["period_err_warn_pct"] = period_warn_pct
                rule["period_err_err_pct"]  = period_err_pct

            rules.append(rule)

    # 중복 제거: (bus가 있으면 bus+id), 없으면 id 기준으로 마지막값 우선
    uniq = {}
    for r in rules:
        key = (r.get("bus", None), r["id"])
        uniq[key] = r
    rules = list(uniq.values())

    # 정렬(보기 좋게)
    def sort_key(r):
        v = r["id"]
        if isinstance(v, int):
            return v
        if isinstance(v, str) and v.lower().startswith("0x"):
            return int(v, 16)
        try:
            return int(v)
        except:
            return 0
    rules.sort(key=sort_key)

    return {"rules": rules}

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--excel", required=True, help="CAN DB 엑셀 경로")
    ap.add_argument("--outdir", default="./db_out", help="출력 폴더")
    ap.add_argument("--only", choices=["SYSTEM", "DRIVING", "BOTH"], default="BOTH")

    ap.add_argument("--bus", type=int, default=None,
                    help="bus 필드 넣고 싶으면 0/1 입력. 안 넣으려면 생략")

    ap.add_argument("--id-dec", action="store_true",
                    help="id를 10진 정수로 출력(기본은 '0x520' 형태의 문자열)")

    ap.add_argument("--ttl-warn-mult", type=float, default=3.0, help="ttl_warn_ms = period * 이 값")
    ap.add_argument("--ttl-err-mult", type=float, default=10.0, help="ttl_err_ms  = period * 이 값")
    ap.add_argument("--perr-warn", type=float, default=20.0, help="period_err_warn_pct 기본값")
    ap.add_argument("--perr-err", type=float, default=50.0, help="period_err_err_pct 기본값")

    args = ap.parse_args()

    excel = Path(args.excel).expanduser().resolve()
    outdir = Path(args.outdir).expanduser().resolve()
    outdir.mkdir(parents=True, exist_ok=True)

    if args.only in ("SYSTEM", "BOTH"):
        obj = build_rules(excel, "CAN DB_LINE2(SYSTEM)",
                          bus=args.bus,
                          id_as_decimal=args.id_dec,
                          ttl_warn_mult=args.ttl_warn_mult,
                          ttl_err_mult=args.ttl_err_mult,
                          period_warn_pct=args.perr_warn,
                          period_err_pct=args.perr_err)
        p = outdir / "vms_rules_SYSTEM_R13.json"
        p.write_text(json.dumps(obj, ensure_ascii=False, indent=2), encoding="utf-8")
        print("OK:", p)

    if args.only in ("DRIVING", "BOTH"):
        obj = build_rules(excel, "CAN DB_LINE1(DRIVING)",
                          bus=args.bus,
                          id_as_decimal=args.id_dec,
                          ttl_warn_mult=args.ttl_warn_mult,
                          ttl_err_mult=args.ttl_err_mult,
                          period_warn_pct=args.perr_warn,
                          period_err_pct=args.perr_err)
        p = outdir / "vms_rules_DRIVING_R13.json"
        p.write_text(json.dumps(obj, ensure_ascii=False, indent=2), encoding="utf-8")
        print("OK:", p)

if __name__ == "__main__":
    main()