# Spesifikasi Protokol Data & Alur Sistem - IoTDrainage (IFMS)

Dokumen ini mendefinisikan arsitektur sistem, alur state-machine firmware, spesifikasi protokol uplink/downlink (MQTT), dan REST API HTTP berdasarkan codebase asli **Intelligent Flood Monitoring System (IFMS)**. Dokumen ditujukan sebagai referensi bagi tim pengembang Firmware, Backend, dan Mobile App.

---

## Bab 1: PENDAHULUAN & ARSITEKTUR

Sistem **IoTDrainage / IFMS (Intelligent Flood Monitoring System)** menggunakan ESP32-CAM (AI-Thinker) sebagai core processing unit. Arsitektur sistem mengadopsi _Clean Architecture_ dengan kapabilitas pengiriman data dual-channel:
1. **MQTT (Message Queuing Telemetry Transport)**: Digunakan untuk pengiriman data telemetri (sensor level air, curah hujan), status sistem, serta menerima perintah dari server (downlink).
2. **HTTP/REST API**: Digunakan khusus untuk transmisi payload yang berat seperti pengiriman foto/snapshot kondisi drainase ke server backend.

**Topologi & Perangkat Keras:**
- **Mikrokontroler**: ESP32-CAM AI-Thinker
- **Sensor Jarak/Air**: Ultrasonic AJ-SR04M (Pin Trig: 12, Pin Echo: 13)
- **Sensor Hujan**: Rain Sensor Module (Pin DO: GPIO 15, Active LOW, mengaktifkan deep sleep wake-up EXT0)
- **Kamera**: OV2640 dengan Flash LED (Pin 4)
- **LED Indikator**: On-board Red LED (Pin 33)

---

## Bab 2: ALUR LOGIKA FIRMWARE (STATE MACHINE)

Firmware menggunakan model _State Machine_ dari saat _booting_ hingga _deep sleep_.

### A. Commissioning Mode
Mode ini aktif saat alat pertama kali dinyalakan (sebelum dikonfigurasi) atau saat dilakukan _factory reset_. Alat bertindak sebagai Access Point (AP) untuk konfigurasi awal via Web UI.

```mermaid
graph TD
    A[Start Commissioning] --> B[Throttle CPU ke 80MHz]
    B --> C[Init Sensors, WiFi AP, WebUI]
    C --> D[Blink LED Cepat 200ms]
    D --> E{Client Terhubung?}
    E -- Tidak --> F{Idle > 2 Menit?}
    F -- Ya --> G[Timeout / Selesai]
    F -- Tidak --> E
    E -- Ya --> H{Config Disimpan?}
    H -- Ya --> I[Reboot Perangkat]
    H -- Tidak --> J{Timeout > 10 Menit?}
    J -- Ya --> G
    J -- Tidak --> K[Kirim Telemetri WS / Snapshot Kamera]
    K --> E
    G --> L[Kembalikan CPU 240MHz & Cleanup]
    L --> M[Pindah ke Operational Mode]
```

**Alur Commissioning:**
1. **CPU Throttling**: Menurunkan kecepatan CPU ke 80MHz untuk mencegah *overheating* (thermal mitigation) karena WiFi AP memakan banyak daya.
2. **Inisialisasi**: Menyalakan mode WiFi AP dan Web UI (Captive Portal). LED merah akan berkedip sangat cepat (200ms).
3. **Standby**: Alat menunggu klien tersambung. Jika tidak ada klien selama 2 menit, atau klien terhubung tapi tidak ada aksi sampai 10 menit, mode ini akan *timeout*.
4. **Interaksi Web UI**: Jika klien terhubung, alat mengirimkan data sensor (telemetri) per detik via WebSocket. Klien bisa meminta *preview* kamera.
5. **Penyimpanan Config**: Jika klien menyimpan pengaturan (SSID, Threshold, dll), alat akan *reboot* ke Operational Mode. Jika *timeout*, alat keluar dari mode commissioning dan paksa masuk Operational Mode dengan konfigurasi *default*.

### B. Operational Mode
Mode utama saat alat berfungsi memantau drainase. Dirancang secara spesifik agar _run-to-completion_ dengan pola hidup sebentar dan tidur lelap (_Deep Sleep_).

```mermaid
graph TD
    A[Wake Up / Start] --> B[Init Rain Sensor]
    B --> C{Koneksi Jaringan}
    C -- Gagal 3x --> D[Pindah Mode OFFLINE]
    C -- Sukses --> E[NTP Time Sync]
    E --> F[Ambil 15 Sampel Ultrasonik]
    F --> G[Jalankan Self-Check]
    G --> H{Kalkulasi Status}
    H -- Normal < 40cm --> I[Status: NORMAL]
    H -- Waspada >= 40cm --> J[Status: WASPADA]
    H -- Bahaya >= 80cm --> K[Status: BAHAYA]
    I --> L[Transmisi MQTT Telemetri]
    J --> L
    K --> L
    L --> M{Status BAHAYA atau Jam 07:00?}
    M -- Ya --> N[Ambil Foto & Upload HTTP]
    M -- Tidak --> O[Putuskan Koneksi Jaringan]
    N --> O
    O --> P{Hitung Waktu Deep Sleep}
    P --> Q[Masuk Deep Sleep]
```

**Alur Operational:**
1. **Cek Rain Sensor**: Mengecek apakah modul terbangun dari _Deep Sleep_ karena interupsi sensor hujan (EXT0 pin 15).
2. **Koneksi Jaringan**: Terhubung ke WiFi lalu MQTT broker. Jika gagal dalam 3 percobaan, beralih ke mode **OFFLINE** (berjalan dengan _exponential backoff_).
3. **Time Sync**: Sinkronisasi waktu menggunakan NTP (pool.ntp.org).
4. **Pembacaan Sensor**: Membaca sensor ultrasonik dengan 15 sampel, lalu difilter menggunakan *median* dan *smoothing alpha* (0.4).
5. **Self-Check**: Mengecek validitas data (mendeteksi _spike_, _variance_ tinggi, atau tersangkut/stuck).
6. **Kalkulasi Status & Sleep Interval**:
   - `NORMAL`: Jarak air < 40cm. Waktu tidur 60 menit.
   - `WASPADA`: Jarak air >= 40cm. Waktu tidur 10 menit.
   - `BAHAYA`: Jarak air >= 80cm (atau ada _Force Bahaya_). Waktu tidur 2 menit.
7. **Transmisi UPLINK**: Mem-publish data sensor (telemetri) via MQTT dengan indikator lampu LED berkedip lambat satu kali (sukses).
8. **Manajemen Kamera**:
   - Jika status `BAHAYA`, kamera akan memotret dan melakukan HTTP POST ke backend. Jika gagal, akan di-flag untuk *retry* maksimal 5 kali pada siklus berikutnya.
   - Jika waktu menunjukkan pukul 07:00 pagi, ambil *Daily Snapshot* lalu kirim ke backend.
9. **Deep Sleep**: Memutuskan WiFi & MQTT secara *graceful*, menyimpan data status sementara di RTC Memory, lalu masuk ke status tidur lelap (*Deep Sleep*).

---

## Bab 3: SPESIFIKASI DATA UPLINK

Perangkat mempublikasikan data secara otomatis (_telemetry_) pada setiap siklus _wakeup_.

### A. Data Telemetri (Normal Operation)
- **Topic MQTT**: `compro9.26.telyu-iot-drainage-be/sensor-data`
- **QoS**: 1

**Kamus Data (Data Dictionary):**

| Key (JSON) | Tipe Data | Deskripsi & Contoh |
|---|---|---|
| `device_id` | String | ID Perangkat berbasis MAC Address (ex: `ESP32-1A2B3C`) |
| `location` | String | String lokasi perangkat yang tersimpan di memori NVS |
| `water_level_cm` | Float | Ketinggian air terukur dalam sentimeter |
| `water_distance` | Float | Jarak mentah sensor ke permukaan air dalam sentimeter |
| `status` | String | Status tinggi air: `"NORMAL"`, `"WASPADA"`, atau `"BAHAYA"` |
| `sensor_flag` | String | Status diagnostik mandiri. Normal = `"OK"` |
| `rain_detected` | Boolean | `true` jika hujan terdeteksi via EXT0 wake, `false` jika tidak |
| `rain_intensity` | Float | `1.0` (jika hujan), `0.0` (jika tidak) |
| `rssi_dbm` | Integer | Kekuatan sinyal WiFi dalam dBm |
| `time_synced` | Boolean | `true` jika perangkat sukses sinkron NTP |
| `timestamp` | Integer | Waktu Unix (_Unix Timestamp_) saat pengiriman |
| `last_upload_failed` | Boolean | (Opsional) `true` jika foto BAHAYA sebelumnya gagal di-upload |

**Contoh Payload JSON (Uplink):**
```json
{
  "device_id": "ESP32-A1B2C3",
  "location": "Drainase_Sektor_Utara",
  "water_level_cm": 45.5,
  "water_distance": 104.5,
  "status": "WASPADA",
  "sensor_flag": "OK",
  "rain_detected": true,
  "rain_intensity": 1.0,
  "rssi_dbm": -67,
  "time_synced": true,
  "timestamp": 1716892334,
  "last_upload_failed": false
}
```

### B. HTTP POST (Upload Foto)
Kamera mem-bypass MQTT untuk upload gambar _snapshot_ atau _bahaya_.
- **Endpoint Foto Bahaya**: `POST http://<HOST>:<PORT>/api/image`
- **Endpoint Snapshot Harian**: `POST http://<HOST>:<PORT>/api/devices/{device_id}/snapshot`
*(Sistem backend harus siap menerima binary multipart form-data image/jpeg).*

---

## Bab 4: SPESIFIKASI PERINTAH DOWNLINK & ACKNOWLEDGMENT

Perangkat mendengarkan _Command Topic_ saat sedang bangun. MQTT bersifat asinkron, balasan/ACK dari command dieksekusi di siklus _Operational_ selanjutnya (atau _Maintenance Mode_).

- **Topic Downlink**: `ifms/<device_id>/cmd`
- **Autentikasi (Token)**: Wajib disertakan `token` berupa _HMAC-SHA256_ dari (Device Secret + Timestamp), dan nilai `ts` (Unix Timestamp). *(Catatan: dilewati/bypass jika secret kosong dalam dev-mode)*.

**Daftar Command yang Didukung:**

1. **`enter_maintenance`** -> Memaksa sistem masuk ke _Maintenance Mode_ pada siklus berikutnya (misalnya untuk update OTA atau kalibrasi Web UI).
2. **`reboot_setup`** -> Melakukan `Factory Reset` di NVS dan *reboot* ulang kembali ke status awal perakitan (_Commissioning Mode_).
3. **`snapshot`** / **`diagnostic`** -> Memaksa perangkat pindah ke maintenance untuk menangkap gambar atau memberikan diagnostik khusus.

**Contoh Payload JSON (Downlink Command):**
```json
{
  "cmd": "enter_maintenance",
  "token": "a1b2c3d4e5f6...",
  "ts": 1716892350
}
```
*(Saat ini ACK/Acknowledge tidak langsung dipublish di topic khusus, namun tercermin pada state cycle berikutnya di payload UPLINK atau perubahan sistem secara fisik).*

---

## Bab 5: MANAJEMEN ERROR & DIAGNOSTIK

Firmware dilengkapi dengan logika **Progressive Fault Escalation** jika terjadi malfungsi sensor (ultrasonik kotor, sinyal memantul acak, atau macet):

- **> 2 Kali Error berturut-turut**: Flag peringatan -> `"SENSOR_UNSTABLE"`
- **> 4 Kali Error berturut-turut**: Memaksa masuk mode -> `"BAHAYA"` (_Force Bahaya_ untuk mitigasi terburuk)
- **> 10 Kali Error berturut-turut**: Perangkat di-pause. (Tidur lelap / Sleep selama 60 menit untuk mencegah kehabisan baterai).

### A. Daftar `sensor_flag` (Self-Check Diagnostic):
- `"OK"` : Sensor bekerja dengan baik.
- `"SENSOR_FAULT"` : Kesalahan generik/tidak terbaca.
- `"SENSOR_UNSTABLE"` : _Variance_ (deviasi sampel) > 50 cm². (Noise tinggi).
- `"SPIKE_DETECTED"` : Lonjakan perubahan jarak > 30 cm dalam satu siklus mendadak.
- `"SENSOR_STUCK"` : Nilai sensor tidak berubah selama 5 siklus.
- `"SENSOR_SUBMERGED"` : Permukaan air menempel pada sensor (Jarak dekat berlebihan).
- `"SENSOR_DISPLACED"` : Nilai _Baseline drift_ tinggi.

### B. Payload Diagnostik Khusus (Dipublikasikan jika perlu)
- **Topic**: `ifms/<device_id>/diagnostic`

**Contoh Payload Diagnostik UPLINK (Error Analysis):**
```json
{
  "type": "sensor_diagnostic",
  "device_id": "ESP32-A1B2C3",
  "sample_count": 15,
  "median_cm": 150.2,
  "variance": 65.4,
  "min_cm": 140.0,
  "max_cm": 175.5,
  "sensor_status": "SENSOR_UNSTABLE"
}
```

### C. Manajemen Error WiFi (Offline Mode Backoff)
Jika gagal koneksi WiFi, firmware menerapkan _exponential backoff_ (Mode OFFLINE) untuk tidur:
- **Gagal 1-3x**: Retries tiap 5 menit (300 detik)
- **Gagal 4-6x**: Retries tiap 15 menit (900 detik)
- **Gagal 7-10x**: Retries tiap 60 menit (3600 detik)
- **Gagal >10x**: Retries tiap 6 jam (21600 detik)
