# Lab 3 · Đề 2 · Giám sát môi trường vườn trồng

ESP32 chạy FreeRTOS, đọc DHT22 và gửi nhiệt độ/độ ẩm đến dashboard qua MQTT. Máy bơm và quạt được điều khiển bằng hai nút Manual. Máy phun sương chạy tự động theo độ ẩm không khí.

## Chạy Wokwi
 
[Mở mô phỏng đã lưu](https://wokwi.com/projects/480656607152456705) và bấm ▶ để chạy ngay. Các bước dưới đây dùng khi cần tạo bản riêng từ mã nguồn.

1. Mở [ESP32 mới trên Wokwi](https://wokwi.com/projects/new/esp32). Thay nội dung hai tab bằng `sketch.ino` và `diagram.json` trong gói này.
2. Ở dấu mũi tên cạnh các tab, chọn **New file…** rồi tạo `mist_policy.h` và `libraries.txt`. Dán nội dung của từng tệp tương ứng.
3. Bấm nút ▶. Serial Monitor dùng 115200 baud. Mỗi 2,5 giây task đọc sẽ in một mẫu.
4. Nhấp DHT22 để đổi nhiệt độ/độ ẩm. Theo dõi LED xanh dương và nhật ký `[MIST]`.

## Chạy dashboard

Mở terminal trong thư mục này và chạy `python3 -m http.server 8000`. Mở [localhost:8000](http://localhost:8000). Trang cần Internet để tải MQTT.js và dùng broker. Có thể đưa ba tệp `index.html`, `style.css`, `dashboard.js` lên máy chủ tĩnh. Dashboard chỉ mở nút điều khiển khi nhận trạng thái mới từ ESP32.

| ESP32 | Thiết bị mô phỏng |
|---|---|
| 3V3, GND, GPIO27 | VCC, GND, SDA của DHT22. SDA kéo lên 3V3 qua 10 kΩ |
| GPIO26 | Điện trở 220 Ω → LED đỏ đại diện máy bơm |
| GPIO25 | Điện trở 220 Ω → LED xanh lá đại diện quạt |
| GPIO33 | Điện trở 220 Ω → LED xanh dương đại diện phun sương PWM |
| GND | Cực âm của cả ba LED |

DHT22 đo độ ẩm **không khí**. LED đại diện cơ cấu chấp hành. Với phần cứng thật, chọn driver công suất phù hợp tải và nguồn riêng. Tỷ lệ PWM là tín hiệu đặt, công suất thực tế phụ thuộc driver và tải.

| Độ ẩm | PWM | Thời lượng mỗi đợt |
|---|---:|---:|
| Dưới 30% | 100% | 30 s |
| 30% đến dưới 35% | 50% | 20 s |
| 35% đến dưới 40% | 25% | 10 s |
| Từ 40% trở lên | 0% | Tắt |

Mỗi đợt đang chạy giữ mức PWM đã chọn lúc bắt đầu. Mẫu ≥40%, lỗi hoặc mẫu quá hạn 6 s sẽ dừng phun sớm. Nếu vẫn khô, hệ thống nghỉ 5 s rồi chọn lại mức theo mẫu mới nhất. Với độ ẩm >60%, có thể bật quạt Manual. Độ ẩm đất và hạ ẩm tự động thuộc phạm vi mở rộng.

## Kiểm tra MQTT

ESP32 dùng `broker.hivemq.com:1883`. Web dùng `wss://broker.hivemq.com:8884/mqtt`. Cả hai dùng gốc `lab3/de2/vuon-dht22-5c8f14`.

| Hậu tố topic | Hướng | Dữ liệu |
|---|---|---|
| /telemetry | ESP32 → Web | JSON có temperature, humidity, uptime (ms), valid |
| /state | ESP32 → Web | JSON có pump, fan (0/1), mist (0/25/50/100) |
| /availability | ESP32 → Web | online/offline, retained và Last Will |
| /cmd/pump | Web → ESP32 | 0 hoặc 1, QoS 0, non-retained |
| /cmd/fan | Web → ESP32 | 0 hoặc 1, QoS 0, non-retained |

Đây là broker công cộng cho mô phỏng. Khi chạy nhiều bản song song, đổi cùng một tiền tố trong `sketch.ino` và `dashboard.js` để tránh nhận lẫn dữ liệu. Khi chạy trên ESP32 thật, thay thông số Wi-Fi cho mạng của mình và chọn board ESP32 dual-core.

## Tệp và kiểm thử

- `sketch.ino` chứa ba task FreeRTOS, Wi-Fi/MQTT, GPIO và Serial.
- `mist_policy.h` chứa thuật toán chọn mức, giữ đợt và nghỉ giữa đợt.
- [Báo cáo PDF](Bao_cao_Lab3_De2.pdf) và ảnh minh chứng [mạch và Serial](wokwi-serial.jpg), [phun sương 25%](wokwi-mist.jpg).
- [Tải bộ bài Lab đầy đủ](Lab3_De2_GiamSatVuon.zip). Giải nén gói này để chạy các lệnh kiểm thử bên dưới.
- `g++ -std=c++11 tests/mist_test.cpp -o /tmp/mist_test && /tmp/mist_test` kiểm thử thuật toán trên máy tính.
- Khi trình diễn, kiểm tra với 50%, 38%, 32%, 25%, rồi về 45%. Trên dashboard, bật/tắt cả bơm và quạt rồi đối chiếu nút, LED và Serial.
- `node tests/dashboard_test.cjs` kiểm thử logic dashboard với sự kiện MQTT mô phỏng. Kiểm tra hai số đo, hai biểu đồ, bốn lệnh Manual, phản hồi và khóa nút khi mất kết nối.
- Wokwi đã biên dịch và xác nhận DHT22 25 °C / 50% RH, MQTT online và PWM 25% ở 37,5% RH. Hai nút Manual đã qua kiểm thử với sự kiện MQTT mô phỏng. Luồng trực tiếp từ trình duyệt qua broker tới ESP32 cần xác minh khi demo.
