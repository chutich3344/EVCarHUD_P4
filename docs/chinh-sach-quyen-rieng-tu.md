# Chính sách quyền riêng tư – HUD Trạm sạc

Cập nhật: 03/11/2026 · Ứng dụng: **EVCarHUD Charge – Trạm sạc** (`com.evcarhud.charge`) · Nhà phát triển: EVCarHUD (liên hệ: support@evcarhud.com)

HUD Trạm sạc tìm trạm sạc ô tô điện gần bạn và gửi thông tin trạm lên màn hình HUD EVCarHUD qua Bluetooth. Chúng tôi không yêu cầu tài khoản, không hiển thị quảng cáo và không bán dữ liệu.

## 1. Dữ liệu ứng dụng sử dụng

| Dữ liệu | Dùng để | Gửi đi đâu |
|---|---|---|
| Vị trí chính xác (GPS) | Tìm trạm sạc quanh bạn, tính khoảng cách và hướng tới trạm | Toạ độ được gửi kèm truy vấn tới nguồn dữ liệu trạm bạn chọn (xem mục 2). Không lưu lại trên máy chủ của chúng tôi để nhận diện bạn. |
| Thiết bị Bluetooth lân cận | Tìm và kết nối HUD EVCarHUD | Không gửi đi đâu; địa chỉ HUD chỉ lưu trên điện thoại. |
| Cài đặt trong app (bán kính, nguồn dữ liệu, khoá API bạn tự nhập) | Ghi nhớ lựa chọn của bạn | Chỉ lưu trên điện thoại. |

Ứng dụng **không** thu thập tên, số điện thoại, email, danh bạ, ảnh, hay mã định danh quảng cáo.

## 2. Bên thứ ba nhận toạ độ khi tìm trạm

Tuỳ nguồn bạn chọn trong Cài đặt, toạ độ hiện tại được gửi trong yêu cầu tìm trạm tới:

- **evcarhud.com** (máy chủ của chúng tôi) – toạ độ được làm tròn trước khi gửi.
- **OpenStreetMap Overpass API** (overpass-api.de và máy chủ dự phòng).
- **Open Charge Map** (api.openchargemap.io) – chỉ khi bạn bật nguồn này.
- **Máy chủ tuỳ chỉnh** – chỉ khi bạn tự nhập địa chỉ.

Dữ liệu trạm VinFast được tải dưới dạng tệp từ GitHub (không kèm vị trí). Các dịch vụ trên có chính sách riêng và có thể ghi nhật ký truy cập (địa chỉ IP, thời gian) theo thông lệ.

## 3. Chạy khi tắt màn hình

Khi bạn bấm **Bắt đầu**, ứng dụng chạy một dịch vụ nền có thông báo cố định để tiếp tục đọc GPS và gửi trạm gần nhất lên HUD khi màn hình tắt. Dịch vụ dừng khi bạn bấm **Dừng** hoặc tắt từ thông báo. Ứng dụng không dùng quyền vị trí nền ngoài phiên này.

## 4. Lưu trữ và xoá dữ liệu

Mọi cài đặt nằm trên điện thoại của bạn. Gỡ ứng dụng hoặc xoá dữ liệu ứng dụng sẽ xoá toàn bộ. Chúng tôi không giữ hồ sơ người dùng nên không có dữ liệu nào cần yêu cầu xoá.

## 5. Trẻ em

Ứng dụng dành cho người lái xe, không hướng tới trẻ em dưới 13 tuổi.

## 6. Thay đổi

Khi chính sách thay đổi, chúng tôi cập nhật trang này và ngày ở đầu trang.

## 7. Liên hệ

support@evcarhud.com
