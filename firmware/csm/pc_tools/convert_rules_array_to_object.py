import json, argparse
from pathlib import Path

def to_hex_keys(cid: int):
    # vms 로더가 어떤 형태로 key를 읽든 걸리게 여러 키를 동시에 생성
    hxU = f"0X{cid:X}"       # 0X300
    hxu = f"0x{cid:X}"       # 0x300
    nop = f"{cid:X}"         # 300
    nopl = f"{cid:x}"        # 300 (소문자)
    return [hxU, hxu, nop, nopl]

def get_rule_list(obj):
    if isinstance(obj.get("rules"), list):
        return obj["rules"]
    if isinstance(obj.get("items"), list):
        return obj["items"]
    raise SystemExit("입력 JSON에 rules/items 배열이 없음")

def parse_id(rule):
    # 기존 파일에 있는 여러 필드 중 하나로라도 ID를 뽑음
    for k in ("can_id", "id_dec", "id", "can_id_hex", "id_hex"):
        if k not in rule:
            continue
        v = rule[k]
        if isinstance(v, int):
            return v
        if isinstance(v, str):
            v = v.strip()
            if not v:
                continue
            try:
                return int(v, 0)      # 0x.. 또는 숫자
            except:
                try:
                    return int(v, 16) # "300" 같은 경우
                except:
                    pass
    return None

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--in", dest="inp", required=True)
    ap.add_argument("--out", dest="out", default="")
    ap.add_argument("--bus", type=int, default=0)
    args = ap.parse_args()

    inp = Path(args.inp).resolve()
    obj = json.loads(inp.read_text(encoding="utf-8"))
    rules = get_rule_list(obj)

    rules_map = {}

    for r in rules:
        cid = parse_id(r)
        if cid is None:
            continue

        name = r.get("name") or r.get("Name") or r.get("title") or r.get("Title") or ""
        exp  = r.get("exp_ms") or r.get("cycle_ms") or r.get("period_ms")
        ttl  = r.get("ttl_ms") or r.get("TTLms")

        rule_obj = {
            "name": name,
            "Name": name,
            "title": name,
            "Title": name,

            "exp_ms": exp,
            "cycle_ms": exp,
            "period_ms": exp,
            "ExpectedCycleMs": exp,

            "ttl_ms": ttl,
            "TTLms": ttl,

            "bus": args.bus,
            "ext": 0,
            "rtr": 0,
        }

        for key in to_hex_keys(cid):
            rules_map[key] = rule_obj

    out_path = Path(args.out).resolve() if args.out else inp.with_name(inp.stem + "_OBJ.json")
    out_obj = {
        "meta": obj.get("meta", {}),
        "rules": rules_map,      # ✅ 핵심: rules를 "객체(map)"로 제공
        "rules_list": rules,     # 배열도 같이 보존(혹시 로더가 배열을 보는 경우 대비)
    }
    out_path.write_text(json.dumps(out_obj, ensure_ascii=False, indent=2), encoding="utf-8")
    print("OK:", out_path)

if __name__ == "__main__":
    main()