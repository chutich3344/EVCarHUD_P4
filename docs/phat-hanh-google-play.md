# Phát hành HUD Trạm sạc lên Google Play

App: **EVCarHUD Charge** (HUD Trạm sạc) · gói `com.evcarhud.charge` (cố định vĩnh viễn sau lần tải lên đầu tiên) · minSdk 26 · targetSdk 36.

Các bước có dấu 👤 là việc bạn tự làm (cần tài khoản hoặc khoá ký). Phần còn lại đã có sẵn trong repo.

## 1. 👤 Tạo khoá tải lên (upload key), làm một lần

Trên máy có Java (Android Studio có sẵn `keytool`):

```bash
keytool -genkeypair -v -keystore hud-upload.jks -alias hud -keyalg RSA -keysize 2048 -validity 10000
# Windows PowerShell:  [Convert]::ToBase64String([IO.File]::ReadAllBytes("hud-upload.jks")) > hud-upload.b64
base64 -w0 hud-upload.jks > hud-upload.b64
```

Cất `hud-upload.jks` và mật khẩu ở nơi an toàn (không commit vào git). Vì dùng **Play App Signing**, đây chỉ là khoá tải lên: nếu mất, bạn xin Google đặt lại được, không mất app.

## 2. 👤 Thêm Secrets vào repo

GitHub → repo EVCarHUD_P4 → Settings → Secrets and variables → Actions → New repository secret:

| Tên | Giá trị |
|---|---|
| `HUD_KEYSTORE_BASE64` | nội dung file `hud-upload.b64` |
| `HUD_KEYSTORE_PASSWORD` | mật khẩu keystore |
| `HUD_KEY_ALIAS` | `hud` |
| `HUD_KEY_PASSWORD` | mật khẩu khoá (thường trùng mật khẩu keystore) |

Sau đó chạy lại workflow **Build app Tram sac** (Actions → Run workflow). Bản build có thêm artifact `hud-tram-sac-play-<commit>` chứa file **.aab** để tải lên Play. `versionCode` = số lần chạy workflow nên luôn tăng.

Lưu ý: từ đây APK trong release `app-tram-sac` cũng ký bằng khoá mới. Máy đang cài bản cũ (ký debug) phải gỡ app rồi cài lại một lần.

## 3. 👤 Tạo app trên Play Console

All apps → **Create app** → tên "EVCarHUD Charge", package `com.evcarhud.charge`, ngôn ngữ mặc định Tiếng Việt, App, Miễn phí. Đây là app riêng, khác app `com.evcarhud.app` đang có.

## 4. 👤 Khai báo trong mục "App content"

- **Privacy policy**: URL công khai tới nội dung `docs/chinh-sach-quyen-rieng-tu.md` (nên đăng thành trang `evcarhud.com/privacy-tram-sac`; Play không nhận file PDF hay trang cần đăng nhập).
- **Ads**: Không có quảng cáo.
- **App access**: Toàn bộ chức năng dùng được không cần đăng nhập. Ghi chú cho người duyệt: "Cần HUD EVCarHUD để thấy trạm trên HUD; không có HUD, app vẫn tìm và hiện trạm trên màn hình điện thoại."
- **Content rating**: điền bảng câu hỏi IARC, chọn danh mục Tiện ích/Bản đồ, trả lời "Không" cho mọi nội dung nhạy cảm → kết quả "Mọi lứa tuổi".
- **Target audience**: 18+ (người lái xe).
- **Data safety** (khai đúng như sau):
  - Thu thập: **Vị trí → Vị trí chính xác** và **Vị trí gần đúng**. Mục đích: Chức năng ứng dụng. Không bắt buộc người dùng? → Bắt buộc (app cần vị trí để chạy). Xử lý tạm thời (ephemeral): Có.
  - Chia sẻ: **Có**, vị trí được gửi tới bên thứ ba (OpenStreetMap Overpass, Open Charge Map) để tìm trạm.
  - Mã hoá khi truyền: Có (HTTPS). Người dùng yêu cầu xoá dữ liệu: không lưu dữ liệu trên máy chủ.
  - Không thu thập: thông tin cá nhân, tài chính, danh bạ, ảnh, mã định danh thiết bị.
- **Foreground service permissions** (bắt buộc vì targetSdk 34+): khai 2 loại
  - *Location*: "Tiếp tục đọc GPS khi tắt màn hình để cập nhật trạm sạc gần nhất lên HUD trong lúc lái xe; do người dùng bấm Bắt đầu, có thông báo cố định và nút Dừng."
  - *Connected device*: "Giữ kết nối Bluetooth LE với HUD EVCarHUD để gửi tên, khoảng cách và hướng trạm sạc."
  - Kèm link video ngắn (YouTube không công khai, 30–60 giây): mở app → Bắt đầu → thông báo hiện → tắt màn hình → HUD vẫn cập nhật.
- **Location permissions**: app **không** xin `ACCESS_BACKGROUND_LOCATION`, nên không phải khai mục vị trí nền.
- **Rủi ro cần biết**: app có quyền `REQUEST_IGNORE_BATTERY_OPTIMIZATIONS` (xin bỏ tối ưu pin để HUD không mất trạm). Google chỉ cho một số trường hợp dùng quyền này. Nếu bị từ chối, đổi sang mở danh sách cài đặt pin (không cần quyền) rồi nộp lại.

## 5. 👤 Trang cửa hàng (Main store listing)

Văn bản có sẵn trong `docs/play-store-mo-ta.md`. Hình ảnh cần:

- Biểu tượng 512×512 PNG.
- Ảnh nổi bật (feature graphic) 1024×500.
- Tối thiểu 2 ảnh chụp màn hình điện thoại (khuyên 4–6), tỉ lệ 9:16, cạnh ngắn ≥ 1080 px nếu muốn được đề xuất.

## 6. 👤 Test rồi mới lên Production

1. **Internal testing**: tải `.aab` lên, thêm email người test, cài qua link. Không cần duyệt.
2. **Closed testing**: nếu tài khoản nhà phát triển là cá nhân tạo sau 13/11/2023, bắt buộc **≥ 12 người test tham gia liên tục 14 ngày** trước khi được mở Production. Mở kênh này sớm.
3. **Production**: Apply for production → trả lời câu hỏi về đợt test → gửi duyệt (thường vài ngày).

Mỗi lần cập nhật: đẩy code → workflow build `.aab` mới (versionCode tự tăng) → tải lên kênh tương ứng.
