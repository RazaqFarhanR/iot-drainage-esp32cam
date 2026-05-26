# Panduan Integrasi Backend (Sistem Deteksi Banjir)

Dokumen ini ditujukan bagi **Backend Developer** yang akan membangun sisi *server/backend* untuk menerima dan mengontrol perangkat Sistem Deteksi Banjir (IFMS). 

Perangkat keras (ESP32-CAM) berkomunikasi menggunakan dua protokol secara bersamaan:
1. **MQTT**: Untuk pengiriman data telemetri rutin, metrik kesehatan (diagnostik), dan penerimaan perintah (*Command*).
2. **HTTP/REST API**: Untuk pengiriman *file* gambar (foto drainase) dan pengambilan konfigurasi sistem terbaru secara dinamis.

---

## 1. Arsitektur Infrastruktur Backend yang Disarankan
*   **MQTT Broker**: Menggunakan sistem broker seperti Mosquitto atau EMQX untuk menangani koneksi *pub/sub* dari perangkat keras.
*   **API Server (Backend)**: Menggunakan Node.js, Python (FastAPI/Django), atau Go untuk menyediakan *endpoint* HTTP dan bertindak sebagai *MQTT Subscriber* independen yang memproses data dari broker ke database.
*   **Database**: 
    *   *Time-Series Database* (InfluxDB atau TimescaleDB) sangat disarankan untuk menyimpan runtutan (*time-series*) data level air historis.
    *   *Relational Database* (PostgreSQL/MySQL) untuk menyimpan data kredensial pengguna dan tabel konfigurasi masing-masing perangkat.
    *   *Object Storage* (S3 / MinIO / Folder Lokal) untuk menyimpan *file* biner berformat gambar `.jpeg`.

---

## 2. Integrasi via MQTT

Broker Anda harus selalu hidup menerima transmisi (*publish*) dari perangkat. Modul *Backend* Anda harus melakukan *subscribe* secara kontinu ke topik-topik berikut:

### 2.1. Topik Telemetri (Data Sensor Utama)
*   **Topik Publikasi dari Device**: `ifms/{device_id}/telemetry`
*   **Tujuan**: Menerima laporan siklus rutin data jarak muka air, status banjir, deteksi curah hujan, dan indikator kekuatan sinyal jaringan (*RSSI*).
*   **Contoh Payload (JSON)**:
    ```json
    {
      "device_id": "DEV-01",
      "location": "Titik Drainase A",
      "water_level_cm": 15.5,
      "raw_distance_cm": 150.0,
      "status": "NORMAL",
      "sensor_flag": "OK",
      "rain_detected": false,
      "rssi_dbm": -65,
      "time_synced": true,
      "timestamp": 1715424500,
      "last_upload_failed": false
    }
    ```
    *(Catatan: Field `last_upload_failed` bernilai `true` apabila sebelumnya perangkat gagal mengirimkan foto kejadian banjir ke server HTTP. Ini dapat menjadi acuan backend untuk meminta alat mengulang pengiriman foto).*

### 2.2. Topik Diagnostik (Kesehatan Sistem)
*   **Topik Publikasi dari Device**: `ifms/{device_id}/diagnostic`
*   **Tujuan**: Menerima paket komputasi analitik terkait kesehatan operasional sensor ultrasonik (perhitungan statistika ping data).
*   **Contoh Payload (JSON)**:
    ```json
    {
      "type": "sensor_diagnostic",
      "device_id": "DEV-01",
      "sample_count": 10,
      "median_cm": 150.0,
      "variance": 0.5,
      "min_cm": 149.0,
      "max_cm": 151.0,
      "sensor_status": "OK"
    }
    ```

### 2.3. Topik Komando (Backend $\rightarrow$ Perangkat)
*   **Topik Tujuan Publikasi dari Backend**: `ifms/{device_id}/cmd`
*   **Tujuan**: Memerintahkan perangkat dari *server* untuk melakukan suatu tindakan (seperti *force take photo* atau *reset*).
*   **Contoh Payload (JSON)**:
    ```json
    {
      "cmd": "take_snapshot",
      "token": "a1b2c3d4e5f6...", 
      "ts": 1715424550
    }
    ```
    *(Catatan: Token digunakan untuk validasi keamanan enkripsi HMAC-SHA256 jika diaktifkan. Perangkat otomatis menolak perintah apabila token palsu/kedaluwarsa untuk mencegah serangan siber).*

---

## 3. Integrasi via HTTP (REST API)

Perangkat mikrokontroler dilarang keras menggunakan MQTT untuk mengirim *file* biner berukuran besar (foto). Oleh karena itu, *Backend* yang Anda bangun **wajib menyediakan minimal 3 *endpoint* REST API** berikut:

### 3.1. Pengambilan Konfigurasi Batas (Threshold) & Kalibrasi
*   **Method**: `GET`
*   **Path URL**: `/api/devices/{device_id}/config`
*   **Deskripsi**: Perangkat akan sesekali memanggil API ini (via *HTTP GET*) untuk memutakhirkan parameter batas toleransi banjir (*Normal vs Bahaya*) langsung dari *database* *backend*.
*   **Response yang Diharapkan dari Backend (Status `200 OK`, JSON)**:
    ```json
    {
      "threshold_normal_cm": 50.0,
      "threshold_bahaya_cm": 120.0,
      "height_sensor_cm": 300.0,
      "offset_cm": 2.0
    }
    ```

### 3.2. Endpoint *Upload* Foto Bahaya (Insiden Terdeteksi)
*   **Method**: `POST`
*   **Path URL**: `/api/upload-image`
*   **Content-Type**: `multipart/form-data`
*   **Deskripsi**: Ketika sistem mendeteksi lonjakan tinggi muka air yang melampaui `threshold_bahaya_cm`, ESP32-CAM akan seketika itu mengambil foto bukti resolusi tinggi dan mengeksekusi POST ke tautan ini.
*   **Body (Form-Data)**:
    - Key form data: `image`
    - Value form data: *Binary data (MIME Type: image/jpeg)* — Nama default file dari alat adalah `flood.jpg`.
*   **Response**: Backend harus mengembalikan HTTP Status `200` atau `201` apabila gambar utuh tersimpan di ruang penyimpnan *server*.

### 3.3. Endpoint *Upload* Foto Snapshot (Manual / On-Demand)
*   **Method**: `POST`
*   **Path URL**: `/api/devices/{device_id}/snapshot`
*   **Content-Type**: `multipart/form-data`
*   **Deskripsi**: Dijalankan hanya pada masa pemeliharaan (*Maintenance Mode*) atau secara paksa ketika backend mengirimkan *Command* MQTT berupa perintah `take_snapshot`. Ditujukan untuk pengawasan jarak jauh meskipun sedang tidak ada indikasi banjir.
*   **Body (Form-Data)**: Identik dengan poin 3.2 (Key parameter: `image`).
*   **Response**: Backend harus mengembalikan HTTP Status `200` atau `201`.

---

## 4. Simulasi Alur Penuh (End-to-End Sequence Flow) pada *Backend*

Pola pemrosesan *backend* harus menyesuaikan gaya *Sleep-Wake-Sleep* dari perangkat keras.

1. **Titik Awal (Wake Up)**: ESP32 terbangun dari tidur mendalam (*Deep Sleep*), membaca ketinggian air terkini.
2. **Kasus Keadaan Aman (NORMAL)**:
   - Perangkat mem-*publish* paket JSON berstatus "NORMAL" ke topik MQTT telemetri.
   - Layanan Backend (*Subscriber*) mencegat (*intercept*) data MQTT tersebut dan merekam grafiknya ke dalam InfluxDB untuk pemantauan historis harian.
3. **Kasus Insiden Banjir (BAHAYA)**:
   - ESP32-CAM mendeteksi tinggi air menembus angka kritis. 
   - ESP32-CAM mem-*publish* payload darurat via MQTT $\rightarrow$ Backend langsung memberikan notifikasi peringatan (seperti via antarmuka *WebSocket* ke Dasbor React/Vue atau Push Notification WhatsApp/FCM).
   - Segera setelah itu, kamera mengambil foto bukti drainase $\rightarrow$ Dieksekusi via `HTTP POST` ke *endpoint* API upload gambar backend.
   - Backend memproses foto, menyimpannya di Amazon S3 atau disk server, memvalidasinya, dan merantai/mengaitkan ID foto tersebut dengan data telemetri banjir di basis data utama.
4. **Titik Akhir (Sleep)**: Setelah transaksi logistik ke server HTTP/MQTT sukses maupun gagal, ESP32 memutuskan seluruh koneksi radio (Wi-Fi) dan memasuki hibernasi panjang (*Deep Sleep*) hingga perputaran durasi selanjutnya demi kelangsungan masa hidup baterai alat di lapangan.
