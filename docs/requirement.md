# Spesifikasi Persyaratan Sistem (SRS): Intelligent Flood Monitoring System (IFMS)

Dokumen ini mendefinisikan arsitektur **multi-mode**, tumpukan protokol komunikasi, dan standar teknis tingkat lanjut untuk sistem deteksi banjir berbasis ESP32-CAM (AI-Thinker) tanpa SD Card.

---

## 1. Arsitektur Multi-Mode & Protokol Komunikasi

Sistem beroperasi dalam **tiga mode** yang masing-masing memiliki tujuan berbeda:

| Kebutuhan | Mode Commissioning | Mode Maintenance | Mode Operasional |
| :--- | :--- | :--- | :--- |
| **Tujuan** | Setup & Kalibrasi awal | Diagnostik & cek lapangan | Monitoring banjir aktif |
| **Konektivitas** | Access Point (AP) | Station (STA) | Station (STA) |
| **Protokol Data** | WebSocket (real-time) | MQTT (on-demand) | MQTT + HTTP POST |
| **Konfigurasi** | HTTP Captive Portal | Read-only | Read-only |
| **Status Daya** | Always On ($\approx$ 180–260 mA) | Per-siklus ($\approx$ 15–30 detik) | Per-siklus ($\approx$ 15–30 detik) |
| **Durasi Aktif** | Maks. 10 menit | Maks. 3 menit | Per-siklus |

### 1.1 Logika Boot (Conditional Boot)

Untuk menghindari masuk Commissioning Mode secara tidak perlu (misal setelah mati listrik sesaat), perangkat melakukan pengecekan NVS terlebih dahulu:

```
[POWER ON / RESET]
        │
        ▼
  Cek NVS "wifi_cfg"
  apakah ada & valid?
        │
   ┌────┴────────────┐
  TIDAK             YA
   │                 │
   ▼                 ▼
COMMISSIONING    OPERATIONAL
   MODE             MODE
```

### 1.2 State Machine Lengkap

```
                    [POWER ON]
                        │
            ┌───────────┴──────────────┐
           NVS kosong               NVS valid
            │                          │
            ▼                          ▼
   ┌─────────────────┐       ┌──────────────────┐
   │  COMMISSIONING  │◄──────│   OPERATIONAL    │◄──┐
   │  (AP + WS)      │Double │   (STA + Sleep)  │   │
   └────────┬────────┘Reset  └────────┬─────────┘   │
            │                         │              │
   Save &   │ Timeout                 │ MQTT cmd     │
   Reboot   │ 10 mnt           WiFi   │ "diagnostic" │
            │                 down    ▼              │
            └──────────►  ┌──────────────────┐     │
                          │   MAINTENANCE    │─────┘
                          │   (STA + 3 mnt)  │ Timeout
                          └──────────────────┘
                                   ▲
                          WiFi OK  │
                    ┌──────────────────────┐
                    │    OFFLINE MODE      │
                    │  (Deep Sleep + retry)│
                    │  backoff 5→15→60 mnt │
                    └──────────────────────┘
                          ↑
             WiFi fail + NVS ada credentials
```

### 1.3 Pemicu Transisi Mode

| Pemicu | Dari Mode | Ke Mode |
| :--- | :--- | :--- |
| NVS kosong saat boot | — | Commissioning |
| **Double Reset** (2x tekan < 3 detik) | Operational | Commissioning |
| **Long Press RESET > 10 detik** (factory reset) | Any | Commissioning (NVS bersih) |
| Klik "Save & Reboot" di Web UI | Commissioning | Operational |
| Timeout 10 menit tanpa klien WS | Commissioning | Operational |
| WiFi gagal + **NVS ada credentials** | Operational | **Offline Mode** |
| WiFi gagal + **NVS kosong** | Operational | Commissioning |
| WiFi kembali terhubung | Offline Mode | Operational |
| MQTT command `{"cmd":"diagnostic"}` | Operational | Maintenance |
| Timeout 3 menit | Maintenance | Operational |

---

## 2. Mode Commissioning (Setup & Kalibrasi)

Mode ini aktif ketika perangkat belum dikonfigurasi atau dipicu oleh Double Reset. Dirancang untuk proses instalasi dan kalibrasi fisik di lapangan.

### 2.1 Aktivasi & Mitigasi Thermal

Karena perangkat *Always-On* saat Commissioning, mitigasi panas diterapkan:

- **CPU Clock**: Diturunkan ke **80 MHz** (dari 240 MHz) selama mode ini aktif.
- **Kamera On-Demand**: Frame tidak di-stream terus-menerus; dikirim hanya saat ada permintaan aktif dari klien WebSocket.
- **Timeout Adaptif**: Jika **tidak ada klien WebSocket** yang terhubung dalam **2 menit** pertama, perangkat langsung masuk Mode Operasional tanpa menunggu 10 menit penuh.

### 2.2 Calibration Wizard (Kalibrasi Tanpa Meteran)

Fitur unggulan untuk memudahkan teknisi mengkalibrasi tinggi pemasangan sensor tanpa alat ukur manual:

**Langkah 1 — Ukur Tinggi Sensor ($Height_{sensor}$)**
> Letakkan benda padat (papan, batu, dll.) di dasar saluran tepat di bawah sensor. Nilai `dist_cm` yang terbaca secara otomatis menjadi $Height_{sensor}$.

**Langkah 2 — Verifikasi Level Air**
> Singkirkan benda. Sistem langsung menghitung:
> $$Water\_Level = Height_{sensor} - dist_{current}$$
> Bandingkan dengan perkiraan visual kondisi air saat itu. Jika sesuai, lanjutkan.

**Langkah 3 — Tentukan Threshold secara Visual**
> Gunakan slider interaktif di Web UI untuk menggeser nilai Threshold Normal dan Bahaya sambil melihat posisi relatifnya terhadap tinggi sensor.

**Tampilan Android saat Calibration Wizard:**

```
┌─────────────────────────────┐
│  🔧 MODE KALIBRASI          │
│                             │
│  Jarak Terukur              │
│  ┌─────────────────────┐   │
│  │     142.3 cm        │   │
│  └─────────────────────┘   │
│  [✅ Pakai sbg Height_sensor]│
│                             │
│  Water Level Terhitung:     │
│  ████░░░░░░░  23.8 cm       │
│                             │
│  Threshold Normal : [40] cm │
│  Threshold Bahaya : [80] cm │
│                             │
│  [💾 Simpan & Mulai Monitor]│
└─────────────────────────────┘
```

### 2.3 Alur Sensor & Streaming (WebSocket)

- **Inisialisasi Sensor**: AJ-SR04M sampling setiap **500ms**, jeda antar-ping minimal **50ms**.
- **Filtrasi Median**: 5 sampel terbaru diurutkan dan diambil nilai tengahnya.
- **WebSocket Server**: Berjalan di `ws://192.168.4.1:81/ws`. Payload JSON dikirim setiap **1 detik**:
    ```json
    {
      "type": "telemetry",
      "dist_cm": 120.5,
      "water_level_cm": 34.5,
      "rssi_dbm": -65,
      "rain": false
    }
    ```
- **Frame Kamera**: Dikirim sebagai Binary WebSocket Frame setiap **5 detik** (hanya jika ada klien aktif).
- **Indikator LED**: GPIO 33 berkedip cepat setiap **200ms** — menandakan mode AP aktif.

### 2.4 Form Konfigurasi Web UI (`http://192.168.4.1`)

| Field | Tipe | Keterangan |
| :--- | :--- | :--- |
| **WiFi SSID** | Text | Nama jaringan WiFi tujuan |
| **WiFi Password** | Password | Kata sandi WiFi |
| **Backend Host** | Text | IP/domain server backend |
| **Backend Port** | Number | Port server (default: `3000`) |
| **Tinggi Sensor** ($Height_{sensor}$) | Number (cm) | Auto-fill dari Calibration Wizard |
| **Offset Kalibrasi** | Number (cm) | Koreksi manual selisih pembacaan |
| **Threshold Normal** | Number (cm) | Batas ketinggian air kondisi aman |
| **Threshold Bahaya** | Number (cm) | Batas ketinggian air kondisi bahaya |

---

## 3. Mode Maintenance (Diagnostik Lapangan)

Diaktifkan dari jarak jauh via MQTT atau dari Android untuk memeriksa kondisi sensor dan kamera tanpa harus ke lokasi fisik.

### 3.1 Aktivasi

```
// Topic: ifms/{device_id}/cmd
{ "cmd": "enter_maintenance" }
```

Perangkat akan bangun dari Deep Sleep, terhubung ke WiFi sebagai STA, dan menunggu perintah diagnostik selama **3 menit** sebelum kembali tidur.

### 3.2 Diagnostik Sensor Ultrasonik

Perangkat mengambil **30 sampel** (2× lebih banyak dari operasional normal) dan melaporkan:

```json
// Publish ke: device/{device_id}/diagnostic
{
  "type": "sensor_diagnostic",
  "sample_count": 30,
  "median_cm": 112.9,
  "variance": 0.3,
  "min_cm": 111.5,
  "max_cm": 114.2,
  "sensor_status": "OK"
}
```

| Nilai `sensor_status` | Kondisi |
| :--- | :--- |
| `OK` | Sensor berfungsi normal |
| `UNSTABLE` | Variance > 50 cm² (sensor kotor/terganggu) |
| `FAULT` | Median = 0 atau > 500 cm (sensor rusak) |

### 3.3 On-Demand Snapshot Kamera

```json
// Topic: ifms/{device_id}/cmd
{ "cmd": "snapshot" }
```

ESP32 mengambil foto beresolusi penuh, upload ke backend:

```
POST http://<backend-host>/api/devices/{device_id}/snapshot
```

Android menampilkan foto tersebut untuk inspeksi visual kebersihan lensa dan arah kamera.

### 3.4 Scheduled Daily Snapshot

Setiap **24 jam** pada waktu yang dikonfigurasi (default: **07:00 pagi**), sistem otomatis mengambil 1 foto referensi harian dan menyimpannya ke backend — bahkan saat kondisi **NORMAL**. Berguna untuk audit visual jangka panjang.

### 3.5 Auto Self-Check (Per Siklus Operasional)

Setiap siklus bangun, sistem melakukan pengecekan mandiri sebelum mengirim data:

| Kondisi Terdeteksi | Flag | Aksi |
| :--- | :--- | :--- |
| Median = 0 atau > 500 cm | `SENSOR_FAULT` | Kirim alert ke backend, skip transmisi data |
| Variance antar-sampel > 50 cm² | `SENSOR_UNSTABLE` | Tandai data dengan flag, tetap kirim |
| $D_{final}$ berubah > 30 cm dalam 1 siklus | `SPIKE_DETECTED` | Gunakan nilai median saja, skip smoothing |
| Pembacaan konstan selama 5 siklus berturut | `SENSOR_STUCK` | Kirim alert, masuk Maintenance Mode |

---

## 4. Fitur Keandalan Data (Anti-Error Sampling)

### 4.1 Algoritma Filtrasi

- **Sampling Window**: **15** pembacaan per siklus bangun, jeda antar-ping minimal **50ms**.
- **Median Filter**: Data diurutkan; nilai 0 cm atau > 500 cm dibuang otomatis sebagai artefak.
- **Smoothing Algorithm**: Median baru digabung dengan data historis di RTC Memory:
    $$D_{final} = (0.4 \times Median_{baru}) + (0.6 \times D_{terakhir})$$
- **Bootstrap Nilai Awal**: Pada siklus pertama (RTC Memory kosong), $D_{terakhir} = Median_{baru}$, sehingga $D_{final} = Median_{baru}$.

### 4.2 Siklus Hidup Mode Operasional (Lifecycle)

| Langkah | Aksi | Estimasi Waktu |
| :--- | :--- | :--- |
| **1. Wake-up** | Bangun dari Deep Sleep (timer / EXT0 hujan) | $< 1$ detik |
| **2. Connect** | Fast Connect via BSSID & Channel (NVS) | $\approx 1$ detik |
| **3. Self-Check** | Validasi sensor sebelum pengukuran | $< 0.5$ detik |
| **4. Sync Config** | HTTP GET threshold terbaru dari backend | $\approx 1$ detik |
| **5. Measure** | 15× sampling + median filter + smoothing | $\approx 2$ detik |
| **6. Process** | Tentukan status: Normal / Waspada / Bahaya | $< 0.1$ detik |
| **7. Transmit** | Publish MQTT; jika BAHAYA → HTTP POST foto | $2-10$ detik |
| **8. Sleep** | Putus WiFi → masuk Deep Sleep | $< 0.5$ detik |

**Indikator LED Operasional:**

| Pola LED | Makna |
| :--- | :--- |
| 1× kedip (500ms) | Transmisi berhasil |
| 3× kedip cepat | Transmisi gagal |
| Mati total | Perangkat sedang Deep Sleep |

---

## 5. Manajemen Konektivitas Dinamis (Smart WiFi)

Semua data jaringan disimpan di NVS namespace `"wifi_cfg"`:

- **First Boot**: NVS kosong → terhubung via DHCP → simpan IP, gateway, subnet, BSSID, Channel ke NVS.
- **Fast Connect**: Gunakan konfigurasi statis dari NVS untuk koneksi $< 1$ detik:
    ```
    NVS Keys: "ip", "gw", "mask", "bssid", "channel"
    ```
- **Fallback DHCP**: Jika IP statis gagal dalam **10 detik**, beralih ke DHCP dan perbarui NVS.
- **Auto-Recovery**: Gagal 3× berturut-turut → reset `"wifi_cfg"` → kembali ke Commissioning Mode.

---

## 6. Fitur Kalibrasi & Threshold Dinamis

**Rumus Kalkulasi Level Air:**
$$Water\_Level = Height_{sensor} - Measured\_Distance + Offset$$

Nilai $Height_{sensor}$ dan $Offset$ disimpan di NVS namespace `"sensor_cfg"`.

### 6.1 Pengaturan Threshold Dinamis (Remote Config)

Threshold dapat diperbarui dari Android oleh pengguna **Admin** tanpa *flash* ulang firmware.

**Sinkronisasi via MQTT Command** — Menggunakan topik command:
```json
{
  "cmd": "set_thresholds",
  "normal_max_cm": 40.0,
  "waspada_max_cm": 80.0,
  "baseline_height_cm": 150.0
}
```
*Catatan: Pengambilan konfigurasi (Fetch on Wake) via HTTP GET dinonaktifkan dalam implementasi saat ini, karena backend menangani sinkronisasi threshold melalui perintah MQTT secara langsung.*

**NVS Storage**: Disimpan di namespace `"threshold_cfg"`. Jika backend tidak dapat dihubungi, gunakan nilai NVS terakhir. Jika NVS juga kosong, gunakan nilai default:

| Parameter | Default |
| :--- | :--- |
| `threshold_normal_cm` | 40 cm |
| `threshold_bahaya_cm` | 80 cm |

**Penetapan Threshold:**

| Status | Logika Level Air | Interval Tidur | Aksi Utama |
| :--- | :--- | :--- | :--- |
| **NORMAL** | $< Threshold_{Normal}$ | 60 Menit | Publish MQTT Heartbeat. |
| **WASPADA** | $\ge Threshold_{Normal}$ | 10 Menit | Publish MQTT intensif. |
| **BAHAYA** | $\ge Threshold_{Bahaya}$ | 2 Menit | Snap Foto → HTTP POST ke Backend. |

> [!NOTE]
> Sensor hujan (GPIO 15) dapat memicu Wake-up EXT0 kapan saja, terlepas dari jadwal timer, untuk mendeteksi presipitasi lebih awal.

---

## 7. Konfigurasi Perangkat Keras (Optimasi AI-Thinker)

### 7.1 Pin Sensor & Aktuator

| Komponen | Pin ESP32-CAM | Deskripsi Implementasi |
| :--- | :--- | :--- |
| **AJ-SR04M Trig** | GPIO 14 | Output pulsa 10 $\mu$s. (Aman, bukan strapping pin) |
| **AJ-SR04M Echo** | GPIO 13 | Input pantulan. Pasang resistor *pull-down* 10 k$\Omega$. |
| **Rain Sensor DO** | GPIO 15 | Wake-up EXT0 (aktif LOW). ⚠️ *Strapping pin* — tidak boleh HIGH saat boot. |
| **Status LED** | GPIO 33 | LED Merah on-board. Pola kedip sesuai status operasi. |
| **External LED** | GPIO 12 | Status LED tambahan. ⚠️ *Strapping pin* — pastikan state LOW atau pull-down agar tidak memicu 1.8V boot select saat dinyalakan. |
| **Flash Camera** | GPIO 4 | Aktifkan hanya saat snapshot BAHAYA di kondisi gelap. |

### 7.2 Pin Kamera (OV2640 AI-Thinker)

| Sinyal Kamera | Pin ESP32-CAM |
| :--- | :--- |
| PWDN | GPIO 32 |
| RESET | N/A (-1) |
| XCLK | GPIO 0 |
| SIOD (SDA) | GPIO 26 |
| SIOC (SCL) | GPIO 27 |
| D7–D0 (Y9–Y2) | GPIO 35, 34, 39, 36, 21, 19, 18, 5 |
| VSYNC | GPIO 25 |
| HREF | GPIO 23 |
| PCLK | GPIO 22 |

> [!WARNING]
> **GPIO 12 & GPIO 15** adalah *strapping pins*. GPIO 12 mengatur voltase internal flash LDO (1.8V vs 3.3V). Untuk menjaga kesehatan boot alat, sensor ultrasonik dipindah ke **GPIO 14** yang aman, dan GPIO 12 digunakan untuk LED eksternal (output saja, aman saat boot). Selalu pasang *pull-down* atau pastikan perangkat eksternal tidak aktif saat power ON.

---

## 8. Parameter Monitoring Android (Aplikasi Utama)

Aplikasi Android terhubung ke backend via **WebSocket** untuk data real-time:
```
ws://<backend-host>/ws/devices/{device_id}
```

### 8.1 Fitur Utama Aplikasi

| Fitur | Deskripsi |
| :--- | :--- |
| **Peta Lokasi & Status** | Marker warna: 🟢 Normal, 🟡 Waspada, 🔴 Bahaya |
| **Calibration Wizard** | Panduan kalibrasi interaktif saat Commissioning Mode |
| **Live Monitoring** | Grafik ketinggian air real-time via WebSocket |
| **Manajemen Threshold** | Form ubah threshold (Admin only) → `PUT /api/devices/{id}/config` |
| **On-Demand Snapshot** | Tombol "Ambil Foto Sekarang" → trigger MQTT diagnostic |
| **Galeri Bukti Visual** | Grid foto kejadian banjir & scheduled daily snapshot |
| **Log Diagnostik** | Riwayat `sensor_status` dan flag anomali |
| **Push Notification** | FCM alert ke semua perangkat terdaftar saat status → BAHAYA |
| **Log Jaringan** | Grafik RSSI (dBm) historis untuk diagnosa kualitas sinyal |

---

## 9. Protokol Komunikasi Backend

### 9.1 MQTT — Telemetri & Perintah

| Parameter | Nilai |
| :--- | :--- |
| **Broker** | `broker.emqx.io` (dev) / self-hosted (production) |
| **Port** | 1883 |
| **QoS** | 1 (At Least Once) |
| **Topic Publish Telemetri** | `compro9.26.telyu-iot-drainage-be/sensor-data` |
| **Topic Publish Diagnostik** | `device/{device_id}/diagnostic` |
| **Topic Subscribe Perintah** | `device/{device_id}/cmd` |
| **Topic Publish Response** | `device/{device_id}/res` |
| **Topic Publish Log** | `device/{device_id}/log` |

Contoh payload MQTT telemetri:
```json
{
  "device_id": "ESP32-CAM-001",
  "water_level_cm": 34.5,
  "water_distance": 115.5,
  "rain_detected": false,
  "status": "NORMAL",
  "sensor_flag": "OK",
  "rssi_dbm": -62,
  "time_synced": true,
  "timestamp": 1715612400
}
```

Daftar perintah MQTT yang didukung (`device/{device_id}/cmd`):

| Perintah | Aksi |
| :--- | :--- |
| `{"cmd":"enter_maintenance"}` | Masuk Mode Maintenance |
| `{"cmd":"snapshot"}` | Jadwalkan ambil foto on-demand |
| `{"cmd":"diagnostic"}` | Jadwalkan diagnostik sensor |
| `{"cmd":"reboot_setup"}` | Factory reset dan Reboot ke Commissioning Mode |
| `{"cmd":"update_wifi", "ssid":"...", "pass":"..."}` | Memperbarui WiFi dan reboot |
| `{"cmd":"set_thresholds", "normal_max_cm":40, ...}` | Memperbarui threshold batas air |
| `{"cmd":"force_snapshot"}` | Paksa ambil foto dan upload langsung |
| `{"cmd":"set_flash_mode", "mode":"AUTO"}` | Mengatur mode flash (OFF, ON, AUTO) |

### 9.2 HTTP REST — Upload & Konfigurasi

| Method | Endpoint | Fungsi |
| :--- | :--- | :--- |
| `POST` | `/api/upload-image` | Upload foto BAHAYA (multipart) |
| `POST` | `/api/devices/{id}/snapshot` | Upload foto diagnostik/harian |

*(Catatan: Endpoint `/config` dengan method GET/PUT ditujukan untuk komunikasi antara Backend dan Android, bukan langsung ke ESP32 karena ESP32 menggunakan MQTT)*

**Error Handling HTTP POST**: Jika gagal (timeout / server error), ESP32 tidak menyimpan foto secara lokal (tanpa SD Card). Status kegagalan dicatat di RTC Memory dan dilaporkan pada siklus MQTT berikutnya via field `"last_upload_failed": true`.

---

## 10. Rekomendasi Keamanan & Instalasi

*   **Blind Spot Sensor**: AJ-SR04M tidak dapat membaca objek pada jarak $< 25$ cm. Pasang sensor minimal **30 cm** di atas level air banjir maksimal yang diantisipasi.
*   **Antena Eksternal**: Gunakan modul ESP32-CAM dengan konektor antena IPEX eksternal di area dengan banyak penghalang sinyal.
*   **Proteksi Daya**: Sistem dilengkapi fitur **LVD (Low Voltage Disconnect)** pada SCC. Perangkat mati otomatis jika tegangan baterai 18650 di bawah ambang aman.
*   **Waterproofing Enclosure**: Gunakan enclosure **IP65** atau lebih. Pasang silikon sealant pada semua lubang kabel. Sensor AJ-SR04M tahan percikan air, namun tidak boleh terendam.
*   **Kondensasi Lensa**: Pasang **silica gel 10g** di dalam enclosure, ganti setiap 6 bulan. Gunakan lensa dengan lapisan anti-fog coating.
*   **Persyaratan Hardware Minimum**: Modul ESP32-CAM **wajib memiliki PSRAM** (min. 4 MB) untuk kualitas foto BAHAYA resolusi penuh (UXGA). Tanpa PSRAM, sistem fallback ke SVGA.
*   **Factory Reset**: Tahan tombol **RESET selama > 10 detik** hingga LED berkedip 5× cepat. Semua data NVS dihapus dan perangkat masuk Commissioning Mode dengan konfigurasi bersih.

---

## 11. Penanganan Kasus Kritis & Edge Cases

Bagian ini mendefinisikan mekanisme penanganan kondisi abnormal yang dapat mengancam keandalan sistem.

### 11.1 Offline Mode — Anti-Loop saat WiFi Mati

Jika WiFi tidak tersedia tetapi NVS memiliki credentials yang valid, perangkat **tidak masuk Commissioning Mode**. Sebaliknya, masuk **Offline Mode** dengan strategi backoff eksponensial:

| Jumlah Retry (RTC Counter) | Interval Deep Sleep |
| :--- | :--- |
| Retry 1–3 | 5 menit |
| Retry 4–6 | 15 menit |
| Retry 7–10 | 60 menit |
| Retry > 10 | 6 jam |

Perangkat kembali ke Operational Mode segera setelah WiFi berhasil tersambung.

---

### 11.2 NVS Corrupt saat Power Mati

Menulis konfigurasi ke NVS menggunakan pola **Write-Verify + CRC32**:

1. Hitung CRC32 dari semua nilai konfigurasi.
2. Tulis data ke namespace staging (`"cfg_staging"`).
3. Baca kembali dan verifikasi CRC32.
4. Jika cocok → salin ke namespace aktif (`"wifi_cfg"`, dll.).
5. Jika tidak cocok → hapus staging, data lama tidak disentuh.

Saat boot, jika CRC32 tidak valid → hapus NVS corrupt → masuk Commissioning Mode.

---

### 11.3 Sensor Terendam Air (Progressive Fault Escalation)

Jika `SENSOR_FAULT` terus-menerus terdeteksi:

| Counter Fault (RTC) | Aksi |
| :--- | :--- |
| 1× | Tandai data `sensor_flag: FAULT`, kirim via MQTT |
| 2–3× | Kirim alert `SENSOR_UNSTABLE` ke backend |
| > 3× | Paksa status → **BAHAYA**, kirim alert `SENSOR_SUBMERGED` |
| > 10× | Tidur 60 menit (hemat daya maksimal) |

> [!IMPORTANT]
> Jika sensor tidak bisa membaca apapun dalam waktu lama, sistem mengasumsikan kondisi terburuk — **air sudah mencapai sensor** — dan melaporkan BAHAYA.

---

### 11.4 Rain Sensor False Positive (EXT0 Cooldown)

Untuk mencegah sensor hujan memicu bangun berulang tanpa henti:

- Simpan `rtc_ext0_count` di RTC Memory.
- Setiap bangun via EXT0 → `rtc_ext0_count++`.
- Jika `rtc_ext0_count >= 5` → nonaktifkan EXT0 sementara, paksa timer sleep **5 menit**, reset counter.
- Jika bangun via timer → reset `rtc_ext0_count = 0`.

---

### 11.5 Foto BAHAYA Gagal Terkirim (Retry via RTC Flag)

Karena tidak ada SD Card, foto yang gagal terkirim tidak bisa disimpan. Mekanisme mitigasi:

1. Jika HTTP POST gagal → set flag `pending_upload: true` di RTC Memory.
2. Siklus berikutnya → cek flag → ambil foto baru → coba kirim sebelum pengukuran reguler.
3. Retry maksimal **5 siklus**. Setelah itu, clear flag (foto terlalu stale).
4. Data numerik (MQTT) selalu terkirim meskipun foto gagal.

---

### 11.6 Keamanan MQTT (Token Verification)

Setiap perintah via MQTT harus menyertakan token verifikasi:

```json
{
  "cmd": "snapshot",
  "token": "HMAC-SHA256(device_secret, timestamp)",
  "ts": 1715612400
}
```

- `device_secret` unik per perangkat, disimpan di NVS saat Commissioning.
- Token dengan `ts` lebih dari **60 detik** dari waktu saat ini dianggap expired (tolak replay attack).
- Untuk environment development: gunakan MQTT broker dengan username + password minimal.

---

### 11.7 Keamanan Web UI (PIN Captive Portal)

- Web UI `http://192.168.4.1` dilindungi PIN **4 digit**.
- PIN default ditampilkan di Serial Monitor saat pertama kali Commissioning.
- Salah PIN **3×** → lockout **5 menit** + LED berkedip pola khusus (5× cepat berulang).
- PIN disimpan di NVS namespace `"auth_cfg"`. Factory Reset menghapus PIN.

---

### 11.8 Sinkronisasi Waktu (NTP)

- Sync NTP ke `pool.ntp.org` dilakukan **sekali setiap siklus** setelah WiFi terhubung (timeout 5 detik).
- Epoch time disimpan di RTC Memory bersama nilai `millis()` saat sync:
  ```
  current_time = rtc_epoch_base + (millis() - rtc_millis_base) / 1000
  ```
- Jika NTP gagal → gunakan estimasi dari RTC Memory terakhir, tandai payload dengan `"time_synced": false`.

---

### 11.9 Deteksi Pergeseran Alat (Baseline Drift)

Jika alat bergeser akibat banjir atau vandalisme, `Height_sensor` di NVS menjadi tidak valid:

- Simpan `baseline_7d_avg` (rata-rata water_level 7 hari) di NVS, diperbarui setiap siklus.
- Jika `water_level_saat_ini - baseline_7d_avg > 40 cm` selama **> 3 siklus berturut-turut** dan bukan kondisi BAHAYA → kirim alert `SENSOR_DISPLACED`.
- Android menampilkan notifikasi untuk inspeksi fisik alat.

---

### 11.10 Device ID Unik (Auto-generate dari MAC Address)

Device ID di-generate otomatis dari MAC Address ESP32 saat pertama boot untuk mencegah duplikasi:

```
Format: "IFMS-XXYYZZ"
Dimana XX, YY, ZZ = 3 byte terakhir MAC Address (hex)
Contoh: "IFMS-A1B2C3"
```

Disimpan di NVS namespace `"device_cfg"` key `"device_id"`.

---

### 11.11 Kualitas Foto (Multi-frame Exposure)

Saat mengambil foto BAHAYA:

1. Ambil **3 frame dummy** terlebih dahulu (warmup kamera, buang).
2. Ambil frame dengan flash **OFF** → simpan sementara.
3. Ambil frame dengan flash **ON** → simpan sementara.
4. Bandingkan rata-rata brightness keduanya.
5. Kirim frame dengan nilai brightness terbaik.

---

### 11.12 HTTP Timeout per Fase

| Fase Operasi | Timeout Maksimal |
| :--- | :--- |
| Koneksi ke server | 5 detik |
| Kirim header HTTP | 3 detik |
| Upload foto (per 1 KB chunk) | 2 detik |
| Tunggu response server | 5 detik |
| **Total maksimal keseluruhan** | **30 detik** |

Jika melewati batas → abort, catat `last_upload_failed: true` di RTC Memory, masuk Deep Sleep.

---

### 11.13 Watchdog Timer (WDT) Adaptif

WDT dikonfigurasi ulang per fase untuk efektivitas maksimal sebagai *safety net*:

| Fase | WDT Timeout |
| :--- | :--- |
| Boot & inisialisasi | 30 detik |
| Koneksi WiFi | 15 detik |
| Pengukuran sensor | 10 detik |
| Upload foto HTTP | 45 detik |
| Masuk Deep Sleep | 5 detik |

> [!WARNING]
> Nilai `WDT_TIMEOUT 600` (10 menit) pada implementasi awal terlalu besar dan tidak efektif sebagai safety net. Harus disesuaikan per fase seperti di atas.