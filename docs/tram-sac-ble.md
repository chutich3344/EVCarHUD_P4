# Giao thức BLE "Trạm sạc" (app HUD Trạm sạc → HUD)

App Android `android-tramsac/` tìm trạm sạc gần xe và gửi trạm được chọn xuống HUD.
Đây là dịch vụ GATT **riêng**, chạy song song với HLP (Waze Mod) và GOFA trên cùng HUD.

## Kết nối

- HUD vẫn chỉ nhận **1 điện thoại** (`CONFIG_BT_NIMBLE_MAX_CONNECTIONS=1`). Khi Waze Mod / GOFA trên cùng
  điện thoại đang nối, Android dùng chung đường kết nối đó: app chỉ mở thêm một GATT client, không cần kết nối thứ hai.
- Khi chưa có app nào nối, app tự kết nối theo MAC đã chọn (autoConnect).
- Vì vậy HUD phải đăng ký dịch vụ này ở **cả hai chế độ** Waze và GOFA (thêm vào bảng dịch vụ GATT, không quảng bá UUID này).

## UUID

| Thành phần | UUID | Thuộc tính |
|---|---|---|
| Dịch vụ | `4556d001-8c2a-4b7e-9d3f-5a1c0e7b2f10` | primary |
| STATION | `4556d002-8c2a-4b7e-9d3f-5a1c0e7b2f10` | write, write without response |
| INFO | `4556d003-8c2a-4b7e-9d3f-5a1c0e7b2f10` | read, trả `TRAMSAC1` |

App xin MTU 247 và ghi có phản hồi; mỗi lần ghi là một bản tin trọn vẹn (không có ký tự xuống dòng), tối đa ~140 byte.
Nếu MTU nhỏ, Android tự chia thành "prepare write", nên HUD cần ghép theo `ble_hs_mbuf_to_flat` (đọc trọn `om`).

## Bản tin (UTF-8, ngăn bằng `|`)

```
T1|<tên>|<khoảng cách m>|<phía>|<góc lệch độ>|<kW>|<số cổng>|<số trạm trong bán kính>
```

| Trường | Ý nghĩa |
|---|---|
| tên | Tên trạm, tối đa 60 byte UTF-8, có dấu tiếng Việt, không chứa `|` |
| khoảng cách | Mét, đường chim bay; < 1 km làm tròn 10 m, ≥ 1 km làm tròn 100 m |
| phía | `A` phía trước (±20°), `R` bên phải, `L` bên trái, `B` phía sau (≥110°), `?` chưa biết hướng xe (đứng yên) |
| góc lệch | -180..180, dương = bên phải; rỗng khi phía là `?` |
| kW | Công suất lớn nhất, 0 = không rõ |
| số cổng | 0 = không rõ |
| số trạm | Số trạm tìm thấy quanh xe |

`T1|` (không có trường nào khác) = không có trạm: HUD xoá ô trạm sạc.

Ví dụ: `T1|VinFast Vincom Thảo Điền|850|R|35|60|4|12`

## Nhịp gửi

- Gửi khi trạm/khoảng cách/phía thay đổi (tối đa ~1 lần / 1,5 giây) và nhắc lại mỗi 30 giây.
- HUD nên coi dữ liệu là cũ sau **90 giây** không nhận được bản tin nào và ẩn ô trạm sạc.
- Nút "Gửi thử" trong app gửi trạm giả `Trạm thử EVCarHUD`, 850 m, bên phải, giữ 15 giây.

## Chọn trạm đích từ web (deep link)

Nút "Gửi lên HUD" trên evcarhud.com/tram-sac mở app bằng:

```
evcarhud-tramsac://gui?id=<id>&name=<tên, url-encode>&lat=<lat>&lon=<lon>&kw=<kW, 0 nếu chưa rõ>&ports=<số cổng>
```

Trên Android web gọi bằng Intent URL với `package=com.evcarhud.tramsac` (fallback về link tải APK).
App lưu trạm này làm **trạm đích**: thay vì trạm gần nhất, HUD luôn nhận trạm đích (vẫn bản tin `T1|…`, khoảng cách
và trái/phải tính theo vị trí xe) cho tới khi người dùng bấm "Bỏ chọn trạm đích" trong app.
