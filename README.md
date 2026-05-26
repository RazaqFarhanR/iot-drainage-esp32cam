# Sistem Deteksi Banjir 🌊

![License](https://img.shields.io/badge/license-MIT-blue.svg)
![Platform](https://img.shields.io/badge/platform-ESP32--CAM-lightgrey.svg)
![Framework](https://img.shields.io/badge/framework-Arduino-teal.svg)

Sistem Deteksi Banjir berbasis **ESP32-CAM (AI-Thinker)** yang dirancang untuk memantau level air drainase secara *real-time*. Perangkat cerdas ini menggabungkan pembacaan sensor jarak (ultrasonik), deteksi hujan, dan tangkapan visual (kamera) yang dikirimkan secara berkala menuju server pemantauan melalui protokol MQTT dan HTTP.

Dirancang dengan fokus pada efisiensi daya (menggunakan *Deep Sleep*), stabilitas jaringan, dan arsitektur kode yang *clean* & *modular*.

---

## 🌟 Fitur Utama

- 📏 **Pengukuran Jarak Presisi**: Memanfaatkan sensor ultrasonik (*time-of-flight*) untuk mengukur level tinggi muka air secara berkala.
- 📸 **Tangkapan Visual Drainase**: Modul kamera OV2640 terintegrasi untuk memberikan bukti visual (foto) keadaan lokasi secara *real-time*.
- ⛈️ **Deteksi Hujan**: Menyesuaikan perilaku dan interval pengiriman data saat kondisi hujan.
- 🔋 **Manajemen Daya Efisien**: Mengimplementasikan *Deep Sleep* di antara interval operasi untuk meminimalkan konsumsi daya secara drastis.
- ⚙️ **State Machine Terintegrasi**: Memiliki mode dinamis: `COMMISSIONING` (Captive Portal), `OPERATIONAL` (Pemantauan), `MAINTENANCE` (OTA & Debug), dan `OFFLINE` (Penyelamatan baterai saat jaringan putus).
- 🔐 **Keamanan & Konektivitas**: Komunikasi payload JSON ringan menggunakan MQTT (*Telemetry, Diagnostic, Command*) lengkap dengan verifikasi keamanan berbasis HMAC-SHA256.

---

## 🏗️ Arsitektur Kode (Sisi Hardware)

Sistem ini dikembangkan menggunakan struktur *clean architecture* yang tersusun di dalam folder `src/`:

- **`config/`**: Parameter *pinout* perangkat keras, kredensial bawaan, dan konstanta sistem.
- **`core/`**: Mesin utama yang menangani *State Machine Dispatcher*, NVS (*Non-Volatile Storage*), *Watchdog*, dan pembangkitan ID Perangkat.
- **`connectivity/`**: *Handler* jaringan WiFi, MQTT *client*, *client* HTTP, dan sinkronisasi waktu adaptif (NTP).
- **`sensor/`**: Antarmuka logika pembacaan sensor ultrasonik dan sensor hujan.
- **`camera/`**: *Driver* penangkap gambar/kompresi JPEG dari modul kamera OV2640.
- **`modes/`**: Modul yang memisahkan logika utama dari setiap *state* (Commissioning, Operational, dsb).
- **`web/`**: *Handler* server web lokal untuk penyajian antarmuka konfigurasi *Captive Portal*.

---

## 🛠️ Kebutuhan Perangkat Keras (Hardware)

1. **Board Utama**: ESP32-CAM (AI-Thinker)
2. **Kamera**: Modul Kamera OV2640 (Bawaan ESP32-CAM)
3. **Sensor Jarak**: Ultrasonik (disarankan seri *Waterproof* JSN-SR04T)
4. **Sensor Cuaca**: Sensor Hujan (Sensor digital/analog presipitasi)
5. **Catu Daya**: Modul Step-down / Baterai Li-Ion dengan sirkuit manajemen pengisian daya.

---

## 🚀 Memulai (Getting Started)

### Prasyarat Pengembangan
- Lingkungan IDE: **PlatformIO** (direkomendasikan) atau Arduino IDE (dengan ESP32 Board Manager terinstal).

### Langkah Flashing dan Instalasi
1. *Clone* repositori ini ke komputer lokal Anda.
2. Buka folder proyek menggunakan *code editor* pilihan Anda (seperti VS Code dengan ekstensi PlatformIO).
3. Sambungkan board **ESP32-CAM** ke komputer menggunakan *FTDI Programmer* / modul USB to TTL (pastikan pasokan tegangan cukup di 5V). 
4. Hubungkan (short) pin `GPIO 0` ke `GND` pada ESP32-CAM untuk mengaktifkan mode *Flash*.
5. Jalankan perintah *Build* dan *Upload* pada IDE Anda.
6. Usai berhasil *upload*, lepaskan sambungan `GPIO 0` ke `GND` lalu tekan tombol fisik *Reset* (RST) pada papan rangkaian.
7. Perangkat akan secara otomatis masuk ke mode **COMMISSIONING**. Sambungkan perangkat gawai Anda ke jaringan *Access Point* yang dipancarkan oleh alat.
8. Akses *Captive Portal* lewat *browser* untuk melakukan pendaftaran SSID WiFi target beserta detail koneksi MQTT Broker.

---

## 📖 Dokumentasi Lengkap
Untuk panduan arsitektur konseptual IoT yang lebih komprehensif beserta penjelasan alur teknis yang ditujukan bagi para rekayasawan perangkat lunak, silakan membaca dokumen terkait di folder `docs/`:

- 📄 [**Panduan Integrasi Backend & API**](./docs/backend_integration.md)
- 📄 [**Spesifikasi Kebutuhan Dokumen (Requirements)**](./docs/requirement.md)

---
*Dikembangkan untuk menunjang keamanan lingkungan melalui pemantauan infrastruktur tata air secara cerdas.*
