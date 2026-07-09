
import re
import math
import json
import argparse
from pathlib import Path
from typing import Optional, Any

import pandas as pd

HEX_RE = re.compile(r"0x[0-9A-Fa-f]+")
WS_RE = re.compile(r"\s+")
NUM_RE = re.compile(r"[-+]?\d+(?:\.\d+)?")

DEFAULT_PROFILE = {
    "profile_name": "hamt_like_r13",
    "sheets": {
        "system": "CAN DB_LINE2(SYSTEM)",
        "driving": "CAN DB_LINE1(DRIVING)"
    },
    "header_scan_rows": 20,
    "header_alias": {
        "msg_name": ["name", "message name"],
        "msg_id": ["identifier [hex]", "identifier", "can id", "id"],
        "period_ms": ["repetition rate [ms]", "period(ms)", "period", "cycle"],
        "byte": ["byte"],
        "bit": ["bit"],
        "comment": ["comment / paramter (signal)", "comment / parameter (signal)", "signal", "comment"],
        "length_resolution": ["length / resolution", "length/resolution"],
        "data_offset": ["data offset", "offset"],
        "data_range": ["data range", "range"],
        "operating_range": ["operating data range", "operating range"],
        "timeout": ["timeout value"],
        "description": ["description"]
    }
}


def is_nan(x: Any) -> bool:
    return x is None or (isinstance(x, float) and math.isnan(x))


def s(x: Any) -> str:
    return "" if is_nan(x) else str(x).strip()


def norm_header(x: Any) -> str:
    txt = s(x).lower().replace("\r", " ").replace("\n", " ").replace("\t", " ")
    txt = re.sub(r"[\[\]\(\)]", " ", txt)
    txt = re.sub(r"[/_]", " ", txt)
    txt = WS_RE.sub(" ", txt).strip()
    return txt


def normalize_name(x: Any) -> str:
    txt = s(x)
    if not txt:
        return ""
    txt = txt.replace("\r", " ").replace("\n", " ").replace("\t", " ")
    txt = WS_RE.sub(" ", txt).strip()
    txt = re.sub(r"\s*_\s*", "_", txt)
    txt = WS_RE.sub(" ", txt).strip()
    return txt


def slug_name(x: Any) -> str:
    txt = normalize_name(x).lower()
    txt = re.sub(r"[^a-z0-9]+", "_", txt).strip("_")
    return txt or "unnamed"


def to_rule_id(cid: int, id_as_decimal: bool):
    return int(cid) if id_as_decimal else f"0x{cid:X}"


def id_to_int(v) -> int:
    if isinstance(v, int):
        return v
    if isinstance(v, str) and v.lower().startswith("0x"):
        return int(v, 16)
    return int(v)


def extract_ids(cell: str):
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
    raw = pd.read_excel(excel, sheet_name=sheet, header=None, nrows=max_scan_rows)
    for ridx in range(len(raw)):
        vals = [norm_header(v) for v in raw.iloc[ridx].tolist()]
        has_name = any(v == "name" for v in vals)
        has_id = any("identifier" in v or v == "id" for v in vals)
        has_period = any("repetition rate" in v or "period" in v or "cycle" in v for v in vals)
        if has_name and has_id and has_period:
            return ridx
    return 6


def map_columns(df: pd.DataFrame, profile: dict) -> dict:
    aliases = profile["header_alias"]
    normalized = {col: norm_header(col) for col in df.columns}
    mapped = {}

    for key, alias_list in aliases.items():
        alias_norm = {norm_header(x) for x in alias_list}
        found = None
        for col, ncol in normalized.items():
            if ncol in alias_norm:
                found = col
                break
        if found is None:
            for col, ncol in normalized.items():
                if any(a in ncol for a in alias_norm):
                    found = col
                    break
        mapped[key] = found
    return mapped


def parse_intish(v: Any) -> Optional[int]:
    txt = s(v)
    if not txt:
        return None
    try:
        return int(float(txt))
    except Exception:
        m = re.search(r"\d+", txt)
        return int(m.group()) if m else None


def parse_floatish(v: Any) -> Optional[float]:
    txt = s(v)
    if not txt:
        return None
    try:
        return float(txt)
    except Exception:
        m = NUM_RE.search(txt)
        return float(m.group()) if m else None


def parse_bit_spec(v: Any) -> dict:
    txt = s(v)
    if not txt:
        return {"raw": "", "lo": None, "hi": None, "width": None}

    txt = txt.replace(" ", "")
    txt = txt.replace(",", "..")

    if re.fullmatch(r"\d+\.\d+", txt):
        a, b = txt.split(".", 1)
        if a.isdigit() and b.isdigit():
            hi = int(a)
            lo = int(b)
            if hi >= lo:
                return {"raw": txt, "lo": lo, "hi": hi, "width": hi - lo + 1}

    m = re.fullmatch(r"(\d+)\.\.(\d+)", txt)
    if m:
        a, b = int(m.group(1)), int(m.group(2))
        hi, lo = max(a, b), min(a, b)
        return {"raw": txt, "lo": lo, "hi": hi, "width": hi - lo + 1}

    if re.fullmatch(r"\d+", txt):
        n = int(txt)
        return {"raw": txt, "lo": n, "hi": n, "width": 1}

    return {"raw": txt, "lo": None, "hi": None, "width": None}


def parse_length_resolution(v: Any) -> tuple[Optional[int], Optional[float], str]:
    txt = normalize_name(v)
    if not txt:
        return None, None, ""

    bit_len = None
    factor = None

    m_bit = re.search(r"(\d+)\s*bit", txt, re.IGNORECASE)
    if m_bit:
        bit_len = int(m_bit.group(1))

    m_res = re.search(r"/\s*([-+]?\d+(?:\.\d+)?)", txt)
    if m_res:
        try:
            factor = float(m_res.group(1))
        except Exception:
            pass

    return bit_len, factor, txt


def parse_min_max(v: Any) -> tuple[Optional[float], Optional[float], str]:
    txt = normalize_name(v)
    if not txt:
        return None, None, ""

    m = re.search(r"([-+]?\d+(?:\.\d+)?)\s*(?:to|~|-)\s*([-+]?\d+(?:\.\d+)?)", txt, re.IGNORECASE)
    if m:
        try:
            return float(m.group(1)), float(m.group(2)), txt
        except Exception:
            pass

    nums = [float(x) for x in NUM_RE.findall(txt)]
    if len(nums) >= 2:
        return nums[0], nums[1], txt
    return None, None, txt


def parse_enum_text(v: Any) -> dict:
    txt = normalize_name(v)
    if not txt:
        return {}

    out = {}
    for key, label in re.findall(r"(\d+)\s*[:\-]\s*([^,/;\n]+)", txt):
        out[key] = label.strip()
    return out


def normalize_signal_name(comment: str) -> str:
    txt = normalize_name(comment)
    txt = re.sub(r"\bLow Byte\b", "", txt, flags=re.IGNORECASE)
    txt = re.sub(r"\bHigh Byte\b", "", txt, flags=re.IGNORECASE)
    txt = re.sub(r"\bLSB\b", "", txt, flags=re.IGNORECASE)
    txt = re.sub(r"\bMSB\b", "", txt, flags=re.IGNORECASE)
    txt = WS_RE.sub(" ", txt).strip(" _-")
    return txt


def low_high_base(comment: str) -> tuple[str, Optional[str]]:
    txt = normalize_name(comment)
    if not txt:
        return "", None
    if re.search(r"low byte", txt, re.IGNORECASE):
        base = re.sub(r"low byte", "", txt, flags=re.IGNORECASE)
        return normalize_name(base).strip(" _-"), "low"
    if re.search(r"high byte", txt, re.IGNORECASE):
        base = re.sub(r"high byte", "", txt, flags=re.IGNORECASE)
        return normalize_name(base).strip(" _-"), "high"
    return txt, None


def has_meaningful_text(*vals: Any) -> bool:
    for v in vals:
        if s(v):
            return True
    return False


def build_rule(cid: int, cur_name: str, exp_ms: Optional[float], *, bus: Optional[int],
               id_as_decimal: bool, ttl_warn_mult: float, ttl_err_mult: float,
               period_warn_pct: float, period_err_pct: float) -> dict:
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

    rule["signals"] = []
    return rule


def detect_message_title_only(comment: str, byte_v: Any, bit_v: Any, len_v: Any, range_v: Any, op_v: Any, desc_v: Any) -> bool:
    return bool(normalize_name(comment)) and not has_meaningful_text(byte_v, bit_v, len_v, range_v, op_v, desc_v)


def make_signal(row: pd.Series, colmap: dict, current_byte: Optional[int], next_row: Optional[pd.Series] = None):
    byte_v = row.get(colmap["byte"])
    bit_v = row.get(colmap["bit"])
    comment_v = row.get(colmap["comment"])
    len_v = row.get(colmap["length_resolution"])
    offset_v = row.get(colmap["data_offset"])
    range_v = row.get(colmap["data_range"])
    op_v = row.get(colmap["operating_range"])
    desc_v = row.get(colmap["description"])

    comment = normalize_name(comment_v)
    bit_info = parse_bit_spec(bit_v)
    bit_len, factor, len_text = parse_length_resolution(len_v)
    min_v, max_v, range_text = parse_min_max(range_v)
    enum_map = parse_enum_text(op_v)
    description = normalize_name(desc_v)
    offset = parse_floatish(offset_v)

    byte_num = parse_intish(byte_v)
    if byte_num is None:
        byte_num = current_byte

    if detect_message_title_only(comment, byte_v, bit_v, len_v, range_v, op_v, desc_v):
        return {"kind": "title", "title": comment, "next_byte": current_byte}, 0

    if byte_num is None and not has_meaningful_text(comment_v, len_v, range_v, op_v, desc_v, bit_v):
        return None, 0

    if not comment and not has_meaningful_text(len_v, range_v, op_v, desc_v):
        return None, 0

    reserved = (not comment) or comment.lower().startswith("not defined")

    # low/high byte pair heuristic
    if next_row is not None and comment:
        base, side = low_high_base(comment)
        if side == "low":
            n_comment = normalize_name(next_row.get(colmap["comment"]))
            n_base, n_side = low_high_base(n_comment)
            n_byte = parse_intish(next_row.get(colmap["byte"]))
            if n_byte is None:
                n_byte = byte_num + 1 if byte_num is not None else None
            n_len, n_factor, _ = parse_length_resolution(next_row.get(colmap["length_resolution"]))
            if n_side == "high" and n_base == base and byte_num is not None and n_byte == byte_num + 1:
                sig = {
                    "name": slug_name(base),
                    "label": base,
                    "kind": "physical" if not reserved else "reserved",
                    "byte_order_hint": "little",
                    "db_byte_start_1based": byte_num,
                    "db_byte_length": 2,
                    "db_bit_text": "8..1 + 8..1",
                    "bit_length": 16,
                    "factor": factor if factor is not None else n_factor,
                    "offset": offset,
                    "data_range_text": range_text,
                    "operating_range_text": normalize_name(op_v),
                    "description": description,
                    "reserved": reserved,
                    "source": "low_high_byte_pair"
                }
                if min_v is not None:
                    sig["warn_min"] = min_v
                if max_v is not None:
                    sig["warn_max"] = max_v
                if enum_map:
                    sig["enum"] = enum_map
                return {"kind": "signal", "signal": sig, "next_byte": n_byte}, 1

    # explicit multi-byte length
    if byte_num is not None and bit_len is not None and bit_len > 8 and bit_info["width"] == 8 and comment:
        byte_len = int(math.ceil(bit_len / 8.0))
        sig = {
            "name": slug_name(comment),
            "label": comment,
            "kind": "physical" if not reserved else "reserved",
            "byte_order_hint": "unknown",
            "db_byte_start_1based": byte_num,
            "db_byte_length": byte_len,
            "db_bit_text": bit_info["raw"],
            "bit_length": bit_len,
            "factor": factor,
            "offset": offset,
            "data_range_text": range_text,
            "operating_range_text": normalize_name(op_v),
            "description": description,
            "reserved": reserved,
            "source": "explicit_multibyte"
        }
        if min_v is not None:
            sig["warn_min"] = min_v
        if max_v is not None:
            sig["warn_max"] = max_v
        if enum_map:
            sig["enum"] = enum_map
        return {"kind": "signal", "signal": sig, "next_byte": byte_num + byte_len - 1}, 0

    # single field
    sig_kind = "reserved" if reserved else "raw"
    width = bit_len if bit_len is not None else bit_info["width"]
    if width == 1:
        sig_kind = "flag" if not reserved else "reserved"
    elif enum_map:
        sig_kind = "enum" if not reserved else "reserved"
    elif factor is not None or min_v is not None or max_v is not None:
        sig_kind = "physical" if not reserved else "reserved"

    sig = {
        "name": slug_name(comment if comment else f"byte_{byte_num}"),
        "label": comment if comment else f"byte_{byte_num}",
        "kind": sig_kind,
        "db_byte_1based": byte_num,
        "db_bit_text": bit_info["raw"],
        "bit_length": width,
        "factor": factor,
        "offset": offset,
        "data_range_text": range_text,
        "operating_range_text": normalize_name(op_v),
        "description": description,
        "reserved": reserved,
        "source": "single_row"
    }
    if byte_num is not None and width is not None:
        sig["db_byte_length"] = int(math.ceil(width / 8.0))
    if min_v is not None:
        sig["warn_min"] = min_v
    if max_v is not None:
        sig["warn_max"] = max_v
    if enum_map:
        sig["enum"] = enum_map
    return {"kind": "signal", "signal": sig, "next_byte": byte_num}, 0


def dedupe_signals(signals: list[dict]) -> list[dict]:
    out = []
    seen = set()
    for sig in signals:
        key = (
            sig.get("name"),
            sig.get("db_byte_start_1based", sig.get("db_byte_1based")),
            sig.get("db_bit_text"),
            sig.get("bit_length"),
            sig.get("source"),
        )
        if key in seen:
            continue
        seen.add(key)
        out.append(sig)
    return out


def dedupe_and_sort_rules(rules: list[dict]):
    uniq = {}
    for r in rules:
        key = (r.get("bus", None), r["id"])
        if key in uniq and r.get("signals"):
            old = uniq[key]
            old["signals"] = dedupe_signals(old.get("signals", []) + r.get("signals", []))
            if not old.get("name_en") and r.get("name_en"):
                old["name_en"] = r["name_en"]
            if "message_definition_title" not in old and r.get("message_definition_title"):
                old["message_definition_title"] = r["message_definition_title"]
        else:
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


def load_profile(path: Optional[str]) -> dict:
    if not path:
        return DEFAULT_PROFILE
    p = Path(path).expanduser().resolve()
    return json.loads(p.read_text(encoding="utf-8"))


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
    profile: dict,
    header_row: Optional[int] = None,
):
    hdr = find_header_row(excel, sheet, max_scan_rows=profile.get("header_scan_rows", 20)) if header_row is None else int(header_row)
    df = pd.read_excel(excel, sheet_name=sheet, header=hdr)
    colmap = map_columns(df, profile)

    rules = []
    current_rules = []
    current_name = ""
    current_byte = None
    current_title = ""

    i = 0
    while i < len(df):
        row = df.iloc[i]

        name_col = colmap["msg_name"]
        id_col = colmap["msg_id"]
        per_col = colmap["period_ms"]

        if name_col is not None and not is_nan(row.get(name_col)):
            nm = normalize_name(row.get(name_col))
            if nm:
                current_name = nm

        ids = extract_ids(row.get(id_col) if id_col is not None else None)
        exp_ms = None
        if per_col is not None and not is_nan(row.get(per_col)):
            try:
                exp_ms = float(row.get(per_col))
            except Exception:
                exp_ms = None

        if ids:
            current_rules = [
                build_rule(
                    cid,
                    current_name,
                    exp_ms,
                    bus=bus,
                    id_as_decimal=id_as_decimal,
                    ttl_warn_mult=ttl_warn_mult,
                    ttl_err_mult=ttl_err_mult,
                    period_warn_pct=period_warn_pct,
                    period_err_pct=period_err_pct,
                )
                for cid in ids
            ]
            current_byte = None
            current_title = ""
            rules.extend(current_rules)

        next_row = df.iloc[i + 1] if i + 1 < len(df) else None
        parsed, skip_next = make_signal(row, colmap, current_byte, next_row)

        if parsed and current_rules:
            if parsed["kind"] == "title":
                current_title = parsed["title"]
                for r in current_rules:
                    if current_title:
                        r["message_definition_title"] = current_title
                current_byte = parsed["next_byte"]
            elif parsed["kind"] == "signal":
                sig = parsed["signal"]
                if current_title and "group_title" not in sig:
                    sig["group_title"] = current_title
                for r in current_rules:
                    r["signals"].append(dict(sig))
                current_byte = parsed["next_byte"]

        if skip_next:
            i += skip_next
        i += 1

    return {"rules": dedupe_and_sort_rules(rules)}


def make_out_path(outdir: Path, prefix: str, kind: str, suffix: str) -> Path:
    tail = f"_{suffix}" if suffix else ""
    return outdir / f"{prefix}_{kind}{tail}.json"


def main():
    ap = argparse.ArgumentParser(description="CAN DB Excel -> JSON rule generator (SYSTEM / DRIVING / MERGED)")
    ap.add_argument("--excel", required=True, help="CAN DB 엑셀 경로")
    ap.add_argument("--outdir", default="./db_out", help="출력 폴더")
    ap.add_argument("--profile", default=None, help="profile json 경로. 생략 시 내장 프로파일 사용")
    ap.add_argument("--only", choices=["SYSTEM", "DRIVING", "MERGED", "ALL"], default="ALL")

    ap.add_argument("--system-sheet", default=None, help="SYSTEM 시트명 강제 지정")
    ap.add_argument("--driving-sheet", default=None, help="DRIVING 시트명 강제 지정")
    ap.add_argument("--prefix", default="vms_rules", help="출력 파일 prefix")
    ap.add_argument("--suffix", default="R13", help="출력 파일 suffix. 비우면 접미사 없음")
    ap.add_argument("--header-row", type=int, default=None,
                    help="헤더 줄 0-based 인덱스 강제 지정. 생략 시 자동 탐색")

    ap.add_argument("--system-bus", type=int, default=None, help="SYSTEM 규칙에 bus 넣기")
    ap.add_argument("--driving-bus", type=int, default=None, help="DRIVING 규칙에 bus 넣기")

    ap.add_argument("--id-dec", action="store_true",
                    help="id를 10진 정수로 출력(기본은 0x520 문자열)")
    ap.add_argument("--ttl-warn-mult", type=float, default=3.0, help="ttl_warn_ms = period * 이 값")
    ap.add_argument("--ttl-err-mult", type=float, default=10.0, help="ttl_err_ms = period * 이 값")
    ap.add_argument("--perr-warn", type=float, default=20.0, help="period_err_warn_pct 기본값")
    ap.add_argument("--perr-err", type=float, default=50.0, help="period_err_err_pct 기본값")

    args = ap.parse_args()

    profile = load_profile(args.profile)
    system_sheet = args.system_sheet or profile["sheets"]["system"]
    driving_sheet = args.driving_sheet or profile["sheets"]["driving"]

    excel = Path(args.excel).expanduser().resolve()
    outdir = Path(args.outdir).expanduser().resolve()
    outdir.mkdir(parents=True, exist_ok=True)

    sys_obj = None
    drv_obj = None

    need_system = args.only in ("SYSTEM", "MERGED", "ALL")
    need_driving = args.only in ("DRIVING", "MERGED", "ALL")

    if need_system:
        sys_obj = build_rules(
            excel, system_sheet,
            bus=args.system_bus,
            id_as_decimal=args.id_dec,
            ttl_warn_mult=args.ttl_warn_mult,
            ttl_err_mult=args.ttl_err_mult,
            period_warn_pct=args.perr_warn,
            period_err_pct=args.perr_err,
            profile=profile,
            header_row=args.header_row,
        )
        if args.only in ("SYSTEM", "ALL"):
            p = make_out_path(outdir, args.prefix, "SYSTEM", args.suffix)
            p.write_text(json.dumps(sys_obj, ensure_ascii=False, indent=2), encoding="utf-8")
            print("OK:", p)

    if need_driving:
        drv_obj = build_rules(
            excel, driving_sheet,
            bus=args.driving_bus,
            id_as_decimal=args.id_dec,
            ttl_warn_mult=args.ttl_warn_mult,
            ttl_err_mult=args.ttl_err_mult,
            period_warn_pct=args.perr_warn,
            period_err_pct=args.perr_err,
            profile=profile,
            header_row=args.header_row,
        )
        if args.only in ("DRIVING", "ALL"):
            p = make_out_path(outdir, args.prefix, "DRIVING", args.suffix)
            p.write_text(json.dumps(drv_obj, ensure_ascii=False, indent=2), encoding="utf-8")
            print("OK:", p)

    if args.only in ("MERGED", "ALL"):
        if sys_obj is None:
            sys_obj = build_rules(
                excel, system_sheet,
                bus=args.system_bus,
                id_as_decimal=args.id_dec,
                ttl_warn_mult=args.ttl_warn_mult,
                ttl_err_mult=args.ttl_err_mult,
                period_warn_pct=args.perr_warn,
                period_err_pct=args.perr_err,
                profile=profile,
                header_row=args.header_row,
            )
        if drv_obj is None:
            drv_obj = build_rules(
                excel, driving_sheet,
                bus=args.driving_bus,
                id_as_decimal=args.id_dec,
                ttl_warn_mult=args.ttl_warn_mult,
                ttl_err_mult=args.ttl_err_mult,
                period_warn_pct=args.perr_warn,
                period_err_pct=args.perr_err,
                profile=profile,
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
