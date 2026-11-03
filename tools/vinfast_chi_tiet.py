#!/usr/bin/env python3
"""Ban day du hon cua du lieu tram sac o to VinFast cho CSDL tram sac evcarhud.com.

Du lieu cong khai cua VinFast KHONG co so cong va cong suat (kW); chi co ten, toa do, dia chi,
trang thai hoat dong va thong tin phi gui xe.

  out/vinfast-chi-tiet.json.gz  [["ten", lat, lon, "dia chi", hoat_dong(1/0), phi_gui_xe(1/0), "ma tram"], ...]
"""
import gzip, json, os, sys, urllib.request

CDN = "https://static-cms-prod.vinfastauto.com/locators/"
UA = {"User-Agent": "Mozilla/5.0 EVCarHUD-TramSac-data/1.0"}


def get(url):
    with urllib.request.urlopen(urllib.request.Request(url, headers=UA), timeout=180) as r:
        raw = r.read()
    return gzip.decompress(raw) if raw[:2] == b"\x1f\x8b" else raw


def clean(s):
    return " ".join(str(s or "").split())


def main(out):
    meta = json.loads(get(CDN + "locators-meta.json"))
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
        name = clean(r.get("name") or r.get("address") or "Trạm sạc VinFast")
        active = 0 if str(r.get("charging_status")).upper() == "INACTIVE" else 1
        fee = 1 if r.get("parking_fee") in (True, "1", "true", 1) else 0
        st.append([name, lat, lon, clean(r.get("address")), active, fee, clean(r.get("store_id"))])
    if len(st) < 1000:
        sys.exit(f"Qua it tram ({len(st)}), co the VinFast doi dinh dang: khong ghi de")
    os.makedirs(out, exist_ok=True)
    body = json.dumps(st, ensure_ascii=False, separators=(",", ":")).encode()
    p = os.path.join(out, "vinfast-chi-tiet.json.gz")
    with open(p, "wb") as f:
        f.write(gzip.compress(body, 9))
    print("tram", len(st), "tam ngung", sum(1 for s in st if not s[4]), "co phi gui xe", sum(s[5] for s in st), "gz bytes", os.path.getsize(p))


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else "out")
