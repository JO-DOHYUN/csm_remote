import re
import json
import math
import argparse
from pathlib import Path

import pandas as pd


HEX_RE = re.compile(r"0x[0-9A-Fa-f]+")


def is_nan(x) -> bool:
    return x is None or (isinstance(x, float) and math.isnan(x))


def to_str(x) -> str:
    if is_nan(x):
        return ""
    return str(x).strip()


def extract_ids(identifier_cell: str):
    """
    Identifier 칸에서 0x... 들을 전부 뽑음.
    - "0x504\n0x505\n0x506" => [0x504,0x505,0x506]
    - "0x210\n…\n0x217" => range expand => [0x210..0x217]
    - "LH1:\n0x123\nLH2:\n0x223 ..." => [0x123,0x223,...]
    """
    s = to_str(identifier_cell)
    ids = [int(h, 16) for h in HEX_RE.findall(s)]
    if not ids:
        return []

    # "…", "..." 포함 + 양끝 2개만 있으면 범위 확장
    if ("…" in s or "..." in s) and len(ids) == 2:
        a, b = ids[0], ids[1]
        if a <= b and (b - a) <= 0x200:  # 안전 범위
            return list(range(a, b + 1))

    # 그대로 반환(중복 제거 + 정렬 유지)
    seen = set()
    out = []
    for v in ids:
        if v not in seen:
            seen.add(v)
            out.append(v)
    return out


def parse_bit_field(bit_cell):
    """
    Bit 칸:
      - 8 (단일 비트)  => start_bit=7, len=1
      - "8..1"         => start_bit=0, len=8
      - "8..5"         => start_bit=4, len=4
    bit 번호는 DB 표기 그대로: 1=LSB, 8=MSB 로 가정
    """
    s = to_str(bit_cell)
    if not s:
        return None, None

    # 단일 숫자
    if re.fullmatch(r"\d+", s):
        b = int(s)
        if 1 <= b <= 8:
            return b - 1, 1  # start_bit (LSB=0)
        return None, None

    # range "8..1"
    m = re.fullmatch(r"(\d+)\.\.(\d+)", s)
    if m:
        hi = int(m.group(1))
        lo = int(m.group(2))
        if 1 <= lo <= 8 and 1 <= hi <= 8 and hi >= lo:
            start_bit = lo - 1
            length = hi - lo + 1
            return start_bit, length

    return None, None


def parse_len_and_scale(res_cell, fallback_len=None):
    """
    Length/Resolution 칸:
      - "1 bit"
      - "16 bit / 0.1"
      - "16 bit / 0.001"
      - "32 bit / 1"
    => length_bits, scale
    """
    s = to_str(res_cell)
    if not s:
        return fallback_len, 1.0

    # length
    m = re.search(r"(\d+)\s*bit", s, re.IGNORECASE)
    length_bits = int(m.group(1)) if m else fallback_len

    # scale ("/ 숫자")
    scale = 1.0
    m2 = re.search(r"/\s*([-+]?\d+(?:\.\d+)?)", s)
    if m2:
        try:
            scale = float(m2.group(1))
        except ValueError:
            scale = 1.0

    return length_bits, scale


def infer_signed_from_range(data_range_cell: str) -> bool:
    s = to_str(data_range_cell)
    # "-200 to 200" 같은 표기면 signed로 추정
    return bool(re.search(r"[-−]\s*\d", s))


def infer_endian_from_text(*texts) -> str:
    blob = " ".join(to_str(t) for t in texts).lower()
    if "big-endian" in blob or "big endian" in blob or "msb" in blob:
        return "big"
    return "little"


def parse_nodes(row, node_cols, node_names):
    tx_nodes = []
    rx_nodes = []
    for col_idx, node_name in zip(node_cols, node_names):
        v = to_str(row[col_idx]).lower()
        if v == "tx":
            tx_nodes.append(node_name)
        elif v == "rx":
            rx_nodes.append(node_name)
    return tx_nodes, rx_nodes


def build_json_for_line(excel_path: Path, sheet_name: str, line_tag: str, drop_not_defined: bool):
    df = pd.read_excel(excel_path, sheet_name=sheet_name, header=None)

    # 헤더 row 찾기: col0="Name", col1에 "Identifier"
    header_row = None
    for i in range(min(30, len(df))):
        c0 = to_str(df.iloc[i, 0])
        c1 = to_str(df.iloc[i, 1])
        if c0 == "Name" and "Identifier" in c1:
            header_row = i
            break
    if header_row is None:
        raise RuntimeError(f"[{sheet_name}] 헤더(Name/Identifier)를 찾지 못함")

    # 컬럼 인덱스(네 DB 형식 고정)
    COL_NAME = 0
    COL_ID = 1
    COL_CYCLE = 2

    NODE_COL_START = 3
    NODE_COL_END = 26  # 포함 (ADCU..KPCU)
    node_cols = list(range(NODE_COL_START, NODE_COL_END + 1))
    node_names = [to_str(df.iloc[header_row, c]) for c in node_cols]

    COL_BYTE = 27
    COL_BIT = 28
    COL_SIGNAL = 29
    COL_LENRES = 30
    COL_OFFSET = 31
    COL_RANGE = 32
    COL_OP_RANGE = 33
    COL_TIMEOUT = 35
    COL_DESC = 36

    messages = {}
    current_ids = []
    current_meta = {}

    def ensure_message(cid: int, meta: dict):
        key = f"0x{cid:X}"
        if key not in messages:
            messages[key] = {
                "id_hex": key,
                "line": line_tag,
                "name": meta.get("name", ""),
                "cycle_ms": meta.get("cycle_ms", None),
                "tx_nodes": meta.get("tx_nodes", []),
                "rx_nodes": meta.get("rx_nodes", []),
                "message_desc": meta.get("message_desc", ""),
                "signals": [],
            }
        else:
            # 비어있는 메타는 보강
            if not messages[key].get("name") and meta.get("name"):
                messages[key]["name"] = meta["name"]
            if messages[key].get("cycle_ms") is None and meta.get("cycle_ms") is not None:
                messages[key]["cycle_ms"] = meta["cycle_ms"]
            if not messages[key].get("tx_nodes") and meta.get("tx_nodes"):
                messages[key]["tx_nodes"] = meta["tx_nodes"]
            if not messages[key].get("rx_nodes") and meta.get("rx_nodes"):
                messages[key]["rx_nodes"] = meta["rx_nodes"]
            if not messages[key].get("message_desc") and meta.get("message_desc"):
                messages[key]["message_desc"] = meta["message_desc"]

    # 순차 파싱
    for r in range(header_row + 1, len(df)):
        row = df.iloc[r, :]

        name_cell = row[COL_NAME]
        id_cell = row[COL_ID]
        cycle_cell = row[COL_CYCLE]

        byte_cell = row[COL_BYTE]
        bit_cell = row[COL_BIT]
        sig_cell = row[COL_SIGNAL]

        # 이 줄에서 id가 언급되면: 새 메시지로 전환(메시지 헤더 줄이든, 첫 시그널 줄이든)
        ids_here = extract_ids(id_cell)
        if ids_here:
            tx_nodes, rx_nodes = parse_nodes(row, node_cols, node_names)

            # cycle
            cycle_ms = None
            if not is_nan(cycle_cell):
                try:
                    cycle_ms = int(float(cycle_cell))
                except Exception:
                    cycle_ms = None

            # message name (없을 수도 있음)
            msg_name = to_str(name_cell)

            # 메시지 설명은 header줄에서는 sig_cell에 들어가는 경우가 있음(예: "VCU Basic information 2")
            msg_desc = ""
            if is_nan(byte_cell) and to_str(sig_cell):
                msg_desc = to_str(sig_cell)

            current_ids = ids_here
            current_meta = {
                "name": msg_name,
                "cycle_ms": cycle_ms,
                "tx_nodes": tx_nodes,
                "rx_nodes": rx_nodes,
                "message_desc": msg_desc,
            }
            for cid in current_ids:
                ensure_message(cid, current_meta)

        # 시그널 줄 판정: Byte/Bit 있고 Signal 텍스트가 있어야 “진짜 시그널”
        if is_nan(byte_cell) or is_nan(bit_cell):
            continue

        sig_name = to_str(sig_cell)
        if not sig_name:
            # 16bit를 2줄로 나누는 “연장 줄(이름 없음)” 같은 케이스는 스킵
            continue

        if drop_not_defined and sig_name.strip().lower() in ("not defined",):
            continue

        # start_byte
        try:
            start_byte = int(float(byte_cell)) - 1  # DB는 1..8, JSON은 0..7
        except Exception:
            continue
        if not (0 <= start_byte <= 7):
            continue

        # bit parse
        start_bit, bit_len_from_bitfield = parse_bit_field(bit_cell)

        # length/scale parse
        length_bits, scale = parse_len_and_scale(row[COL_LENRES], fallback_len=bit_len_from_bitfield)
        if length_bits is None:
            # 최후: 비트필드라도 없으면 1bit로 처리
            length_bits = 1
        if start_bit is None:
            start_bit = 0

        # offset
        offset = 0.0
        if not is_nan(row[COL_OFFSET]):
            try:
                offset = float(row[COL_OFFSET])
            except Exception:
                offset = 0.0

        data_range = to_str(row[COL_RANGE])
        op_range = to_str(row[COL_OP_RANGE])

        endian = infer_endian_from_text(op_range, row[COL_DESC], sig_name)
        is_signed = infer_signed_from_range(data_range)

        # enum 자동 추출(간단 패턴만)
        enum_map = {}
        for m in re.finditer(r"(\d+)\s*[-:]\s*([A-Za-z가-힣0-9_ ]+)", op_range):
            k = int(m.group(1))
            v = m.group(2).strip()
            # 범위 숫자만 있는 경우는 enum으로 취급하지 않음
            if re.fullmatch(r"\d+(\.\d+)?", v):
                continue
            enum_map[k] = v

        sig_obj = {
            "name": sig_name,
            "start_byte": start_byte,
            "start_bit": start_bit,      # 0..7, bit1=0(LSB)
            "length_bits": int(length_bits),
            "endian": endian,            # little/big (DB 텍스트 기반 추정)
            "signed": is_signed,          # data_range에 음수 있으면 signed 추정
            "scale": float(scale),
            "offset": float(offset),
            "data_range": data_range,
            "op_range": op_range,
            "desc": to_str(row[COL_DESC]),
        }
        if enum_map:
            sig_obj["enum"] = enum_map

        # 현재 메시지에 연결
        if not current_ids:
            # 방어: 메시지 컨텍스트가 없으면 id_cell을 강제로 써서라도 연결 시도
            ids_fallback = extract_ids(id_cell)
            if not ids_fallback:
                continue
            current_ids = ids_fallback
            for cid in current_ids:
                ensure_message(cid, current_meta)

        for cid in current_ids:
            key = f"0x{cid:X}"
            messages[key]["signals"].append(sig_obj)

    out = {
        "meta": {
            "source_excel": str(excel_path.name),
            "sheet": sheet_name,
            "line": line_tag,
            "bit_numbering": "DB 표기: bit 1=LSB, bit 8=MSB. JSON start_bit은 0..7 (bit1=0).",
        },
        "messages": messages,
    }
    return out


def load_wireless_fault_map(excel_path: Path):
    df = pd.read_excel(excel_path, sheet_name="WIRELEE_CHARGER_ERROR_CODE", header=None)

    header_row = None
    for i in range(min(50, len(df))):
        row = df.iloc[i].astype(str)
        if row.str.contains("Fault Number", case=False, na=False).any():
            header_row = i
            break
    if header_row is None:
        return {}

    # 헤더 row 기준으로 다시 읽기
    df2 = pd.read_excel(excel_path, sheet_name="WIRELEE_CHARGER_ERROR_CODE", header=header_row)
    # 예상 컬럼명: "Fault Number", "Fault Name"
    fm = {}
    if "Fault Number" not in df2.columns or "Fault Name" not in df2.columns:
        return fm

    for _, r in df2.iterrows():
        if pd.isna(r.get("Fault Number")):
            continue
        try:
            k = int(r["Fault Number"])
        except Exception:
            continue
        v = "" if pd.isna(r.get("Fault Name")) else str(r["Fault Name"]).strip()
        if v:
            fm[k] = v
    return fm


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--excel", required=True, help="C:\WORKS\HAMT2.0\HYM_HAMT2.0_CAN DB_R13_260206.xlsx")
    ap.add_argument("--outdir", default="out_db", help="C:\WORKS\HAMT2.0")
    ap.add_argument("--drop-not-defined", action="store_true", help='Signal명이 "Not defined"면 JSON에서 제외')
    args = ap.parse_args()

    excel_path = Path(args.excel).expanduser().resolve()
    outdir = Path(args.outdir).expanduser().resolve()
    outdir.mkdir(parents=True, exist_ok=True)

    system = build_json_for_line(
        excel_path=excel_path,
        sheet_name="CAN DB_LINE2(SYSTEM)",
        line_tag="SYSTEM",
        drop_not_defined=args.drop_not_defined,
    )
    driving = build_json_for_line(
        excel_path=excel_path,
        sheet_name="CAN DB_LINE1(DRIVING)",
        line_tag="DRIVING",
        drop_not_defined=args.drop_not_defined,
    )
    fault_map = load_wireless_fault_map(excel_path)

    # fault map은 system/driving 둘 다에 같이 넣어도 되고, 별도 파일로 빼도 됨
    system["wireless_charger_fault_map"] = fault_map
    driving["wireless_charger_fault_map"] = fault_map

    system_path = outdir / "system_r13_ui_db.json"
    driving_path = outdir / "driving_r13_ui_db.json"

    system_path.write_text(json.dumps(system, ensure_ascii=False, indent=2), encoding="utf-8")
    driving_path.write_text(json.dumps(driving, ensure_ascii=False, indent=2), encoding="utf-8")

    print("OK")
    print(" -", system_path)
    print(" -", driving_path)
    print(" - fault_map:", len(fault_map), "items")


if __name__ == "__main__":
    main()