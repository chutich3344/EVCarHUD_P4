#!/usr/bin/env python3
"""Tai du lieu tram sac o to VinFast (du lieu cong khai cua trang tim tram sac vinfastauto.com),
giu lai ten + toa do, ghi ra file nho cho app Android tai ve.

  out/vinfast-meta.json       {"generation":..., "count":..., "updated":"..."}
  out/vinfast-tram-sac.json.gz  [["ten", lat, lon], ...]
"""
import gzip, json, os, sys, time, urllib.request

CDN = "https://static-cms-prod.vinfastauto.com/locators/"
UA = {"User-Agent": "Mozilla/5.0 EVCarHUD-TramSac-data/1.0"}


def get(url):
    req = urllib.request.Request(url, headers=UA)
    with urllib.request.urlopen(req, timeout=180) as r:
        raw = r.read()
    return gzip.decompress(raw) if raw[:2] == b"\x1f\x8b" else raw


def main(out):
    meta = json.loads(get(CDN + "locators-meta.json"))
    print("meta", meta)
    d = json.loads(get(CDN + meta["full"]))
    rows = d["data"] if isinstance(d, dict) else d
    st = []
    for r in rows:
        if r.get("category_slug") != "car_charging_station":
            continue
        if str(r.get("status")).lower() in ("0", "false", "inactive"):
            continue
        try:
            lat, lon = round(float(r["lat"]), 6), round(float(r["lng"]), 6)
        except (KeyError, TypeError, ValueError):
            continue
        if not (-90 <= lat <= 90 and -180 <= lon <= 180) or (lat == 0 and lon == 0):
            continue
        name = " ".join(str(r.get("name") or r.get("address") or "Trạm sạc VinFast").split())
        st.append([name, lat, lon])
    if len(st) < 1000:
        sys.exit(f"Qua it tram ({len(st)}), co the VinFast doi dinh dang: khong ghi de")
    os.makedirs(out, exist_ok=True)
    body = json.dumps(st, ensure_ascii=False, separators=(",", ":")).encode()
    with open(os.path.join(out, "vinfast-tram-sac.json.gz"), "wb") as f:
        f.write(gzip.compress(body, 9))
    m = {"generation": meta.get("generation"), "count": len(st), "updated": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())}
    with open(os.path.join(out, "vinfast-meta.json"), "w") as f:
        json.dump(m, f)
    print(m, "gz bytes", os.path.getsize(os.path.join(out, "vinfast-tram-sac.json.gz")))


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else "out")
