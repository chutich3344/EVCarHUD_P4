#!/usr/bin/env python3
"""Thong ke cac muc cong suat tru sac o to VinFast (chay mot lan trong CI, in ra log)."""
import collections, json, re, sys
sys.path.insert(0, "android-tramsac/tools")
from vinfast_data import CDN, get

meta = json.loads(get(CDN + "locators-meta.json"))
d = json.loads(get(CDN + meta["full"]))
rows = [r for r in (d["data"] if isinstance(d, dict) else d) if r.get("category_slug") == "car_charging_station"]
print("so tram o to:", len(rows))
print("mau 1 ban ghi:", json.dumps(rows[0], ensure_ascii=False)[:3000])
keys = collections.Counter(k for r in rows for k in r.keys())
print("cac truong:", keys.most_common())
def walk(o, path=""):
    if isinstance(o, dict):
        for k, v in o.items(): yield from walk(v, path + "." + k)
    elif isinstance(o, list):
        for v in o: yield from walk(v, path + "[]")
    else:
        yield path, o
kw = collections.Counter(); per_station = collections.Counter(); fields = collections.Counter()
for r in rows:
    found = set()
    for p, v in walk(r):
        s = str(v)
        if re.search(r"kw|power|cong_?suat|capacity|output", p, re.I) or re.search(r"\d+(\.\d+)?\s*kW", s):
            for m in re.findall(r"(\d+(?:[.,]\d+)?)\s*kW", s, re.I):
                found.add(float(m.replace(",", ".")))
            if re.fullmatch(r"\d+(\.\d+)?", s) and re.search(r"kw|power|cong_?suat", p, re.I):
                found.add(float(s))
            fields[p] += 1
    for k in found: kw[k] += 1
    per_station[max(found) if found else 0] += 1
print("truong lien quan cong suat:", fields.most_common(20))
print("so tram co tru muc kW (moi tram dem 1 lan moi muc):", sorted(kw.items()))
print("cong suat lon nhat moi tram:", sorted(per_station.items()))
