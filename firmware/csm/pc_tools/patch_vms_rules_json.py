import json, argparse
from pathlib import Path

def parse_any_id(item):
    # 우선순위: can_id(숫자) > id(숫자/문자) > id_hex
    for k in ("can_id", "id_dec", "id", "id_hex", "can_id_hex"):
        if k not in item:
            continue
        v = item[k]
        if isinstance(v, int):
            return v
        if isinstance(v, str):
            v = v.strip()
            if not v:
                continue
            # "0x520" / "0X520" / "520"
            try:
                return int(v, 0)
            except:
                pass
            try:
                return int(v, 16)
            except:
                pass
    return None

def patch(path: Path, bus: int):
    obj = json.loads(path.read_text(encoding="utf-8"))

    rules = obj.get("rules") or obj.get("items")
    if not isinstance(rules, list):
        raise SystemExit("JSON에 rules/items 배열이 없음")

    for it in rules:
        cid = parse_any_id(it)
        if cid is None:
            continue

        id_hex = f"0X{cid:X}"

        # ✅ 핵심: id를 문자열로
        it["id"] = id_hex
        it["ID"] = id_hex
        it["id_hex"] = id_hex
        it["can_id_hex"] = id_hex

        # 숫자도 같이 보존(로더가 숫자를 쓸 수도 있으니까)
        it["id_dec"] = cid
        it["can_id"] = cid

        # bus/ext/rtr 기본값도 같이 넣어줌(키 매칭에 bus 쓰는 경우 대비)
        it.setdefault("bus", bus)
        it.setdefault("Bus", bus)
        it.setdefault("ext", 0)
        it.setdefault("rtr", 0)

    obj["rules"] = rules
    obj["items"] = rules

    out = path.with_name(path.stem + "_patched.json")
    out.write_text(json.dumps(obj, ensure_ascii=False, indent=2), encoding="utf-8")
    print("OK:", out)

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--in", dest="inp", required=True)
    ap.add_argument("--bus", type=int, default=0)
    args = ap.parse_args()
    patch(Path(args.inp).resolve(), args.bus)

if __name__ == "__main__":
    main()