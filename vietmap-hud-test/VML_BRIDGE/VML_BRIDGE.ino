// Cau noi VietMap Live -> HUD P4 (mach ESP32 DevKit WROOM-32).
// P4 chi co BLE (chip C6), VietMap Live can Bluetooth thuong (SPP): mach nay gia lam HUD VietMap H1V,
// tu tra loi goi hoi thong tin (0E00), con lai chuyen NGUYEN KHUNG sang P4 qua UART 921600.
//   DevKit GPIO17 (TX2) -> P4 GPIO5 | DevKit GPIO16 (RX2) <- P4 GPIO4 | VIN 5V | GND
// Them khung trang thai cung dinh dang VML (A5 5A 37 C3, lenh byte 12-13 = FF 03, byte 14 = 1 dien thoai dang noi / 0 chua)
// gui moi 1 s va ngay khi doi, de P4 biet cau noi con song + VML noi hay chua.
// Den xanh tren mach (GPIO2): sang = VML dang noi, nhay cham = cho ket noi.
#include <Arduino.h>
#include <BluetoothSerial.h>
#include <freertos/stream_buffer.h>
#include <cstring>

#define BRIDGE_VER "1.0"
#define BT_NAME "VIETMAP_HUD_EVC"
#define P4_TX 17
#define P4_RX 16
#define P4_BAUD 921600
#define LED 2

static const char h1vInfo[] =
    "\xA5\x5A\x37\xC3\x00\x00\x00\x66\x68\x05\x27\xB8\x0E"
    "MODEL:H1V,HW:1.6.4,FW:1.3.6,PROTOCOL:2.2.2, OBDV:H1_2.1.44C,"
    "COMPILE:Feb 27 2021-12:13:56"
    "\x00\xE6\xD4\x26\x3B\x71\x50\x41\x6C";
static_assert(sizeof(h1vInfo) - 1 == 110, "chuoi H1V phai 110 byte");

static const uint8_t magic[4] = {0xA5, 0x5A, 0x37, 0xC3};
BluetoothSerial bt;
StreamBufferHandle_t phoneRx;
uint8_t packet[16384];
size_t used, expected;
uint32_t lastByteAt, frames, dropped;
bool connected;

static uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }

// Khung trang thai 22 byte: tong = do dai + 8 -> do dai 14
static void sendStatus() {
  uint8_t f[22] = {0xA5, 0x5A, 0x37, 0xC3, 0, 0, 0, 14, 0, 0, 0, 0, 0xFF, 0x03, (uint8_t)(connected ? 1 : 0),
                   (uint8_t)(frames >> 8), (uint8_t)frames, (uint8_t)(dropped > 255 ? 255 : dropped), 0, 0, 0, 0};
  Serial2.write(f, sizeof f);
}

static void onFrame(const uint8_t *p, size_t n) {
  ++frames;
  if (n == 22 && p[12] == 0x0E && p[13] == 0x00) {
    bt.write(reinterpret_cast<const uint8_t *>(h1vInfo), sizeof(h1vInfo) - 1);
    Serial.println("VML hoi thong tin -> tra loi H1V");
    return;
  }
  Serial2.write(p, n);
  Serial.printf("-> P4 lenh %02X%02X, %u byte\n", p[12], p[13], (unsigned)n);
}

static void inspectByte(uint8_t v) {
  if (used < sizeof magic && v != magic[used]) {
    used = (v == magic[0]) ? 1 : 0;
    if (used) packet[0] = v;
    return;
  }
  packet[used++] = v;
  if (used == 8) {
    uint32_t len = be32(packet + 4);
    if (len < 6 || len > sizeof(packet) - 8) {
      used = expected = 0;
      return;
    }
    expected = len + 8;
  }
  if (!expected || used != expected) return;
  onFrame(packet, used);
  used = expected = 0;
}

void setup() {
  Serial.begin(115200);
  Serial2.begin(P4_BAUD, SERIAL_8N1, P4_RX, P4_TX);
  Serial2.setTxBufferSize(16384);
  pinMode(LED, OUTPUT);
  phoneRx = xStreamBufferCreate(32768, 1);
  bt.onData([](const uint8_t *data, size_t length) {
    while (length) {
      size_t c = xStreamBufferSend(phoneRx, data, length, pdMS_TO_TICKS(50));
      if (!c) { ++dropped; return; }
      data += c;
      length -= c;
    }
  });
  Serial.printf("Cau noi VML %s: %s\n", BRIDGE_VER, bt.begin(BT_NAME) ? "SPP san sang, ten " BT_NAME : "SPP LOI");
}

void loop() {
  uint8_t data[512];
  size_t n = xStreamBufferReceive(phoneRx, data, sizeof data, pdMS_TO_TICKS(10));
  uint32_t now = millis();
  if (now - lastByteAt > 1000) used = expected = 0;   // khung dut giua chung: bo
  if (n) lastByteAt = now;
  for (size_t i = 0; i < n; ++i) inspectByte(data[i]);

  static uint32_t lastStatus;
  bool c = bt.hasClient();
  if (c != connected || now - lastStatus >= 1000) {
    if (c != connected) Serial.println(c ? "VML da noi" : "VML ngat");
    connected = c;
    lastStatus = now;
    sendStatus();
  }
  digitalWrite(LED, connected ? HIGH : ((now / 500) & 1));
}
