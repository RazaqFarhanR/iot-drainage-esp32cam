# Panduan Integrasi Backend: Arsitektur IoT Smart Publisher

Dokumen ini disusun sebagai panduan implementasi awal dan pembagian tugas antara sistem **IoT (Firmware)** dan sistem **Backend**. 

Arsitektur perangkat IoT dirancang menggunakan pola **Smart Publisher**. Artinya, perangkat IoT dirancang mandiri untuk melakukan komputasi pembacaan sensor dan penentuan status secara internal, lalu mengirimkan hasilnya secara satu arah (Uplink) ke server. Tujuannya adalah untuk memaksimalkan keandalan alat, mengamankan perangkat dari serangan luar, dan menghemat memori serta baterai (*Deep Sleep*).

Berikut adalah spesifikasi integrasi dan tugas yang harus diimplementasikan oleh tim Backend.

---

## 1. Struktur Payload Telemetri MQTT

Perangkat IoT akan melakukan pengiriman data (*publish*) ke MQTT Broker setiap kali bangun dari mode tidurnya.

**Format Payload (Uplink):**
```json
{
  "device_id": "ESP32-A1B2C3",
  "water_distance": 104.5,
  "water_level_cm": 45.5,
  "status": "WASPADA",
  "rain_detected": true,
  "sensor_flag": "OK",
  "timestamp": 1717645000,
  "next_wakeup_sec": 600
}
```

### Spesifikasi Integrasi:
*   **Penerima Data Matang (Source of Truth)**: IoT bertanggung jawab penuh untuk mengkalkulasi ketinggian air aktual (`water_level_cm`) dan menetapkan `status` banjir (NORMAL / WASPADA / BAHAYA) berdasarkan kalibrasi fisiknya di lapangan. **Backend wajib menggunakan data ini sebagai Source of Truth** dan hanya bertugas menyimpannya ke dalam *database* tanpa perlu menghitung ulang statusnya.
*   **Zona Waktu UTC**: Field `timestamp` dikirim dalam format angka _Unix Epoch_ murni (UTC). Backend/Frontend bertugas menerjemahkan angka ini ke _Timezone_ lokal pengguna saat akan merendernya di layar Dashboard.
*   **Offline Detection (Watchdog)**: Alat menggunakan pola _Dynamic Heartbeat_ dengan field `next_wakeup_sec` (durasi alat akan tidur). Backend wajib memasang program _Cron Job_ secara berkala untuk mendeteksi matinya alat. 
    *   *Rumus Offline:* `Jika (Waktu Server Saat Ini > timestamp + next_wakeup_sec + Toleransi 5 Menit) => Set Status Alat menjadi OFFLINE`.

---

## 2. Struktur Payload Log Sistem (System Logs)

Selain telemetri rutin, perangkat IoT juga akan sesekali mengirimkan log aktivitas atau peringatan sistem (misalnya: perangkat di-restart, gagal sinkronisasi NTP, dll) ke topik MQTT khusus log.

*   **Topik Publikasi Log**: `compro9.26.telyu-iot-drainage-be/sensor-log`
    *(Format disamakan dengan `sensor-data`, namun diakhiri dengan `sensor-log`)*

**Format Payload Log (Uplink):**
```json
{
  "device_id": "IOT-A1B2C3",
  "timestamp": 1717645000,
  "level": "INFO",
  "message": "Perangkat menyala karena Timer (Rutin)",
  "wake_reason": "TIMER",
  "reset_reason": "DEEPSLEEP_RESET",
  "active_time_ms": 2340,
  "wifi_rssi_dbm": -68,
  "network_failures": 0,
  "free_heap_bytes": 145000
}
```

### Kategori Level Log (Severity)
Perangkat IoT mengklasifikasikan pesan log ke dalam 3 tingkatan utama. Tim Backend dapat menggunakan `level` ini sebagai acuan penyaringan (*filtering*) data atau sebagai pemicu alarm peringatan (*Trigger Notification*).

1. **`INFO` (Informasi Operasional Normal)**
   * **Deskripsi:** Mencatat aktivitas rutin yang berjalan sesuai rencana. Tidak memerlukan tindakan apapun dari sistem Backend atau Administrator.
   * **Contoh Kejadian:** `"Perangkat menyala karena Timer (Rutin)"`, `"Perangkat menyala paksa karena curah hujan (EXT0)"`.
2. **`WARNING` (Peringatan Sistem)**
   * **Deskripsi:** Terjadi anomali atau kegagalan kecil pada fitur sekunder, namun perangkat **masih berhasil** menjalankan fungsi utamanya (tidak *crash*).
   * **Contoh Kejadian:** `"Gagal sinkronisasi waktu NTP. Menggunakan estimasi RTC"` *(Alat gagal mendapat jam internet, tapi tetap jalan pakai jam internal).*
3. **`ERROR` (Kesalahan Kritis / Fatal)**
   * **Deskripsi:** Terjadi kegagalan tingkat tinggi yang menggagalkan fungsi utama alat pada siklus tersebut (*Hardware Error* atau jaringan putus total). Backend sangat disarankan untuk membunyikan alarm/notifikasi darurat ke petugas pemeliharaan jika menerima level ini.
   * **Contoh Kejadian:** `"Kamera gagal diinisialisasi. Cek koneksi hardware"`, `"Gagal mengunggah foto BAHAYA setelah batas maksimal percobaan"`.

### Spesifikasi Integrasi:
*   **Pencatatan Log**: Backend sangat disarankan membuat *handler* (fungsi penerima) khusus yang men-*subscribe* topik `sensor-log` ini. Tujuannya adalah agar Backend memiliki jejak rekam (*audit trail*) historis mengenai alasan alat terbangun, atau apakah alat mengalami gagal fungsi (*error*) di lapangan.

---

## 3. Arsitektur Satu Arah (Tanpa Downlink)

Sistem IoT tidak mendengarkan (*subscribe*) ke topik MQTT mana pun. Tidak ada jalur untuk mengirimkan perintah jarak jauh (Remote Commands) dari Backend ke Perangkat.

### Spesifikasi Integrasi:
*   **Hanya Subscriber**: Layanan Backend dirancang murni sebagai pendengar (*Subscriber*) dari topik telemetri IoT. Backend tidak diperkenankan mem-_publish_ pesan atau *command* apapun ke arah IoT.
*   **Pembatasan Fitur Dashboard**: Karena IoT bersifat mandiri, Frontend/Dashboard Admin tidak perlu menyediakan antarmuka (tombol) untuk mengubah konfigurasi WiFi, merestart perangkat, memaksakan foto kamera jarak jauh, ataupun memodifikasi ambang batas banjir ke alat. Semua konfigurasi tersebut murni diurus oleh teknisi lapangan.

---

## 4. Konfigurasi Alat (Captive Portal)

Semua bentuk pengaturan alat di lapangan (Koneksi WiFi, Kalibrasi Jarak Sensor, Batas Threshold Waspada/Bahaya, dll.) dilakukan secara langsung oleh teknisi menggunakan koneksi lokal fisik (memanfaatkan fitur Double Reset perangkat yang akan memunculkan Web UI / Captive Portal dari HP Teknisi). 

### Spesifikasi Integrasi:
*   Dashboard Admin / Backend tidak berperan sebagai pengatur konfigurasi alat. 
*   Jika diperlukan fitur di Dashboard untuk sekadar *mencatat* atau mendokumentasikan nilai _Sensor Height_ atau _Threshold_ untuk keperluan administrasi (tidak disinkronkan ke alat), Backend dapat menyediakan form *Input* biasa di dalam sistem. Namun perlu ditekankan bahwa nilai penentu status yang sebenarnya tetaplah murni berasal dari payload JSON `status` yang dilaporkan IoT.

## 5. Penambahan Topik Baru untuk Kebutuhan Maintenance

Untuk mendukung kegiatan pemeliharaan (*maintenance*) di lapangan tanpa membebani ukuran *payload* pada telemetri rutin, perangkat akan memublikasikan metadata statis ke dalam topik khusus pemeliharaan. Data ini sangat esensial bagi teknisi, salah satunya adalah informasi **IP Address lokal** perangkat.

*   **Topik Publikasi Maintenance**: `compro9.26.telyu-iot-drainage-be/device-info`

**Format Payload (Uplink):**
```json
{
  "device_id": "IOT-A1B2C3",
  "ip_address": "192.168.1.10",
  "location": "Drainase_Sektor_Utara",
  "timestamp": 1717645000
}
```

### Spesifikasi Integrasi:
*   **Frekuensi Pengiriman**: Perangkat IoT hanya akan mengirimkan *payload* ini **satu kali** setiap kali perangkat dihidupkan ulang dari kondisi mati total (*Cold Boot*), atau ketika teknisi mengaktifkan mode konfigurasi (*Local Commissioning*). 
*   **Penyediaan Data Maintenance & Lokasi**: Sistem Backend diharapkan menyediakan *listener* pada topik ini untuk menangkap informasi terbaru (seperti IP Address dan Lokasi lapangan) lalu menyimpannya ke dalam *database* sebagai referensi. Field `location` ini juga dapat dimanfaatkan oleh Backend untuk melengkapi data master perangkat secara otomatis (*Auto-registration*) jika `device_id` belum terdaftar. Informasi ini nantinya dapat ditampilkan di *Dashboard* untuk mempermudah teknisi melacak dan mengakses perangkat.

---

## 6. Protokol HTTP Upload Foto (Multipart Form-Data)

Untuk pengiriman gambar visual (baik kejadian banjir maupun snapshot harian), perangkat IoT menggunakan koneksi HTTP POST langsung ke server backend dengan format **Multipart Form-Data**. Hal ini dikarenakan ukuran payload gambar yang besar (> 100 KB) tidak disarankan dikirimkan via MQTT.

### A. Endpoint API & Kondisi Pemicu
1. **Foto Kejadian Banjir (Status BAHAYA):**
   * **Pemicu:** Ketinggian air menyentuh atau melebihi ambang batas bahaya (`threshold_bahaya` dari NVS).
   * **Endpoint:** `POST /api/image`
2. **Snapshot Harian (Scheduled Daily Snapshot):**
   * **Pemicu:** Waktu sinkronisasi jam internal RTC tepat menunjukkan pukul 07:00 pagi (dapat disesuaikan di firmware).
   * **Endpoint:** `POST /api/devices/{device_id}/snapshot` *(di mana `{device_id}` digantikan dengan ID Perangkat yang bersangkutan, contoh: `POST /api/devices/IOT-A1B2C3/snapshot`)*.

### B. Format Multipart Body Request
Semua berkas foto diunggah menggunakan parameter *key-value* di dalam body request sebagai berikut:

* **`device_id`** (Tipe text/string): ID unik perangkat pengirim (contoh: `IOT-A1B2C3`).
* **`image`** (Tipe file/binary): Berkas biner gambar bertipe JPEG (`image/jpeg`).

### C. Respons yang Diharapkan
Server backend wajib merespons dengan kode status sukses **`200 OK`** atau **`201 Created`** sesegera mungkin saat berkas selesai diterima. 
* Jika respons bukan sukses atau terjadi kegagalan (misalnya server *timeout*), perangkat ESP32-CAM (yang tidak memiliki penyimpanan SD Card fisik) akan:
  1. Mencatat kegagalan di RTC Memory perangkat.
  2. Mengeset flag `pendingUpload = true` untuk mencoba mengunggah foto baru pada siklus bangun berikutnya (maksimal 5 kali percobaan berturut-turut sebelum diabaikan/stale).
  3. Mengirimkan log `ERROR` ke topik MQTT log (`sensor-log`) sebagai pemberitahuan ke pengelola.

---

## Kesimpulan

Arsitektur **Smart Publisher** ini mendistribusikan beban komputasi penentu status genangan langsung ke perangkat tepi (*Edge/IoT*), dan menjadikan Backend sebagai pusat pencatatan data besar (*Data Logger*) serta pusat monitoring (*Watchdog/Dashboard*). Desain ini mengeliminasi risiko kegagalan sinkronisasi konfigurasi antara Cloud dan Hardware di lapangan.
