#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <WiFiClient.h>
#include <EEPROM.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <MD_Parola.h>
#include <MD_MAX72xx.h>
#include <SPI.h>
#include <time.h> // Pustaka standar C untuk fungsi waktu

// --- Definisi Hardware ---
#define HARDWARE_TYPE MD_MAX72XX::FC16_HW
#define MAX_DEVICES 16
#define CS_PIN D6     // Chip Select pin for MAX7219
#define buzzer D8     // Buzzer pin
#define led1 D1       // LED1 pin
#define led2 D2       // LED2 pin
#define BUTTON_PIN D4 // Tombol trigger hardware untuk mengganti tampilan (GPIO2)

// --- Objek Pustaka ---
MD_Parola display(HARDWARE_TYPE, CS_PIN, MAX_DEVICES);
WiFiUDP ntpUDP;
// Offset GMT+7 (25200 detik), interval update NTP 60 detik (60000 ms)
NTPClient timeClient(ntpUDP, "pool.ntp.org", 25200, 60000);
ESP8266WebServer server(80);

// --- Kredensial WiFi ---
const char* ssid1 = "REDMI NOTE 13";
const char* pass1 = "88888888";
const char* ssid2 = "Rena muchtar 4";
const char* pass2 = "RedOnioN4";

// --- Variabel Alarm ---
int alarmHour1 = 6, alarmMin1 = 0;
int alarmHour2 = 7, alarmMin2 = 59;

// --- Variabel Display & Scrolling ---
bool displayTanggal = false; // true = tampilkan tanggal, false = tampilkan jam
bool scrollingActive = false; // Status apakah display sedang scrolling info
int scrollStep = 0;           // Langkah saat scrolling

// --- Variabel Alarm & Jam Tepat (Chime) ---
bool alarmAktif = false;
unsigned long alarmStartTime = 0;
unsigned long alarmLastStepTime = 0;
int alarmStep = 0; // Digunakan untuk pola bunyi & LED alarm/jam tepat

unsigned long lastJamTepatMillis = 0; // Waktu terakhir jam tepat aktif
bool jamTepatActive = false;
unsigned long jamTepatStart = 0;

// --- Variabel Waktu Sistem ---
time_t currentSystemTime = 0; // Menyimpan waktu sistem (dari NTP atau manual)

// --- Variabel Buzzer Per Menit ---
static int lastMinute = -1; // Menyimpan menit terakhir untuk deteksi perubahan
bool minuteChangeActive = false;
unsigned long minuteChangeStartTime = 0;

// --- Variabel Pulse LED2 Saat Scrolling (Fitur Baru) ---
bool led2PulseActive = false;
unsigned long led2PulseStartTime = 0;
const unsigned int LED2_PULSE_DURATION = 200; // Durasi pulse 200ms

// --- Variabel Tombol D4 (Fitur Baru) ---
unsigned long lastButtonPressTime = 0;
const unsigned long DEBOUNCE_DELAY = 50; // Debounce delay in milliseconds
bool lastButtonState = HIGH; // Mengikuti INPUT_PULLUP (tombol belum ditekan)

// --- Konstanta dan Alamat EEPROM ---
const uint16_t EEPROM_MAGIC = 0xA5A5;
const int EEPROM_MAGIC_ADDR = 16; // Alamat untuk magic number EEPROM
const int EEPROM_ALARM1_H_ADDR = 0;
const int EEPROM_ALARM1_M_ADDR = 4;
const int EEPROM_ALARM2_H_ADDR = 8;
const int EEPROM_ALARM2_M_ADDR = 12;

// ------ HTML + JavaScript Web UI -------
// KODE HTML LENGKAP UNTUK WEB UI (sama seperti sebelumnya)
const char htmlPage[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Kontrol Jam & Alarm</title>
<style>
  body {
    font-family: Arial, sans-serif;
    margin: 20px;
    background-color: #1a1a1a; /* Dark background */
    color: #f0f0f0; /* Light text */
  }
  h2, h3 {
    color: #66b3ff; /* Lighter blue for headers in dark mode */
  }
  form {
    background-color: #2a2a2a; /* Slightly lighter dark for form background */
    padding: 20px;
    border-radius: 8px;
    box-shadow: 0 4px 8px rgba(0,0,0,0.3); /* Stronger shadow for contrast */
    margin-bottom: 20px;
  }
  input[type="number"],
  input[type="date"],
  input[type="time"] {
    width: calc(50% - 10px);
    padding: 8px;
    margin-bottom: 10px;
    border: 1px solid #555; /* Lighter border for inputs */
    border-radius: 4px;
    background-color: #3a3a3a; /* Darker input background */
    color: #f0f0f0; /* Light text in inputs */
  }
  button {
    background-color: #007bff; /* Original blue for buttons */
    color: white;
    padding: 10px 15px;
    border: none;
    border-radius: 4px;
    cursor: pointer;
    font-size: 16px;
    margin-right: 10px;
    transition: background-color 0.2s ease; /* Smooth transition on hover */
  }
  button:hover {
    background-color: #0056b3; /* Darker blue on hover */
  }
  #now {
    font-size: 1.2em;
    font-weight: bold;
    margin-bottom: 20px;
  }
</style>
</head>
<body>
<h2>&#x23F0; Kontrol Jam & Alarm</h2> <p id="now">Jam Sekarang: --:--</p>

<h3>Alarm</h3>
<form id="alarmForm">
  Alarm 1:
  <input type="number" name="h1" min="0" max="23" value="%H1%"> :
  <input type="number" name="m1" min="0" max="59" value="%M1%"><br>
  Alarm 2:
  <input type="number" name="h2" min="0" max="23" value="%H2%"> :
  <input type="number" name="m2" min="0" max="59" value="%M2%"><br>
  <button type="submit">&#x1F4BE; Simpan Alarm</button> </form>

<h3>Atur Jam & Tanggal</h3>
<form id="timeForm">
  Tanggal: <input type="date" name="date" id="inputDate">
  Waktu: <input type="time" name="time" id="inputTime"><br>
  <button type="submit">&#x1F4CC; Set Waktu Manual</button> <button type="button" id="syncNTPBtn">&#x1F310; Sinkronkan NTP</button> </form>

<h3>Informasi Jaringan</h3> <p>Status WiFi: <span id="wifiStatus">Memuat...</span></p>
<p>SSID WiFi: <span id="wifiSSID">Memuat...</span></p>
<p>IP Lokal: <span id="localIP">Memuat...</span></p>
<p>Status Hotspot: <span id="apStatus">Memuat...</span></p>
<p>IP Hotspot: <span id="apIP">Memuat...</span></p>

<h3>Kontrol Sistem</h3>
<button id="toggleDisplayBtn">&#x1F5A5;&#xFE0F; Toggle Tampilan</button> <button id="disableAPBtn">&#x274C; Matikan Hotspot</button> <script>
function updateNow(){
  fetch('/time').then(r=>r.text()).then(t=>{
    document.getElementById('now').innerText='Jam Sekarang: '+t;
    const [dateStr, timeStr] = t.split(' ');
    if(dateStr && timeStr) {
      document.getElementById('inputDate').value = dateStr;
      document.getElementById('inputTime').value = timeStr.substring(0,5);
    }
  });
}
setInterval(updateNow,1000);
updateNow();

// --- Fungsi baru untuk update status jaringan ---
function updateNetworkStatus(){
  fetch('/network_status').then(r=>r.json()).then(data=>{
    document.getElementById('wifiStatus').innerText = data.wifiStatus;
    document.getElementById('wifiSSID').innerText = data.wifiSSID;
    document.getElementById('localIP').innerText = data.localIP;
    document.getElementById('apStatus').innerText = data.apStatus;
    document.getElementById('apIP').innerText = data.apIP;
  });
}

setInterval(updateNetworkStatus, 5000); // Update setiap 5 detik
updateNetworkStatus(); // Panggil pertama kali saat halaman dimuat
// --- Akhir fungsi baru ---


fetch('/get_alarms').then(r=>r.json()).then(data => {
  document.querySelector('input[name="h1"]').value = data.h1;
  document.querySelector('input[name="m1"]').value = data.m1;
  document.querySelector('input[name="h2"]').value = data.h2;
  document.querySelector('input[name="m2"]').value = data.m2;
});

document.getElementById('alarmForm').onsubmit = e => {
  e.preventDefault();
  const params=new URLSearchParams(new FormData(e.target));
  fetch('/set_alarm?'+params).then(r=>r.text()).then(alert);
};

document.getElementById('timeForm').onsubmit = e => {
  e.preventDefault();
  const params=new URLSearchParams(new FormData(e.target));
  fetch('/set_manual?'+params).then(r=>r.text()).then(alert);
};

document.getElementById('toggleDisplayBtn').onclick = () => {
  fetch('/toggle_display').then(r=>r.text()).then(alert);
};

document.getElementById('disableAPBtn').onclick = () => {
  fetch('/disable_ap').then(r=>r.text()).then(alert);
};

document.getElementById('syncNTPBtn').onclick = () => {
  fetch('/sync_ntp').then(r=>r.text()).then(alert);
};
</script>
</body>
</html>
)rawliteral";


// ------- Setup Functions ---------
void setupPins(){
  pinMode(buzzer, OUTPUT); noTone(buzzer);
  digitalWrite(buzzer, LOW); // Pastikan buzzer mati di awal
  pinMode(led1, OUTPUT); digitalWrite(led1, LOW);
  pinMode(led2, OUTPUT); digitalWrite(led2, LOW);
  pinMode(BUTTON_PIN, INPUT_PULLUP); // Inisialisasi pin tombol dengan pull-up internal
}

void setupDisplay(){
  display.begin();
  display.setIntensity(5); // Kecerahan display (0-15)
  display.displayClear();
  display.displayText("Starting...", PA_CENTER, 100, 0, PA_PRINT, PA_NO_EFFECT);
  // display.displayAnimate() dipanggil di loop untuk animasi
}

// Fungsi koneksi WiFi non-blocking
void connectWiFi() {
  Serial.println("Connecting to WiFi...");
  WiFi.mode(WIFI_STA); // Mode Station
  WiFi.begin(ssid1, pass1);
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 10) { // Coba 10x
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("\nFailed to connect to SSID1. Trying SSID2...");
    WiFi.begin(ssid2, pass2);
    attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 10) { // Coba 10x
      delay(500);
      Serial.print(".");
      attempts++;
    }
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi connected!");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\nFailed to connect to any known WiFi. Starting Access Point.");
    WiFi.mode(WIFI_AP);
    WiFi.softAP("ESP8266_HOTSPOT","12345678");
    Serial.print("AP IP address: ");
    Serial.println(WiFi.softAPIP());
  }
}

void setupNTP(){
  timeClient.begin();
  timeClient.update(); // Update pertama kali saat startup
  currentSystemTime = timeClient.getEpochTime(); // Dapatkan waktu dari NTP
  Serial.print("NTP time synchronized: ");
  Serial.println(ctime(&currentSystemTime)); // Cetak waktu untuk debug
  
  // Set lastMinute in setup
  struct tm *t_init = localtime(&currentSystemTime);
  lastMinute = t_init->tm_min;
}

void setupEEPROM(){
  EEPROM.begin(512);
  if (EEPROM.read(EEPROM_MAGIC_ADDR)!=EEPROM_MAGIC){
    Serial.println("EEPROM not initialized. Writing default alarms.");
    EEPROM.put(EEPROM_MAGIC_ADDR, EEPROM_MAGIC);
    EEPROM.put(EEPROM_ALARM1_H_ADDR,alarmHour1);
    EEPROM.put(EEPROM_ALARM1_M_ADDR,alarmMin1);
    EEPROM.put(EEPROM_ALARM2_H_ADDR,alarmHour2);
    EEPROM.put(EEPROM_ALARM2_M_ADDR,alarmMin2);
    EEPROM.commit();
  } else {
    Serial.println("EEPROM initialized. Reading alarms.");
    EEPROM.get(EEPROM_ALARM1_H_ADDR,alarmHour1);
    EEPROM.get(EEPROM_ALARM1_M_ADDR,alarmMin1);
    EEPROM.get(EEPROM_ALARM2_H_ADDR,alarmHour2);
    EEPROM.get(EEPROM_ALARM2_M_ADDR,alarmMin2);
  }
}

void saveAlarms(){
  EEPROM.put(EEPROM_ALARM1_H_ADDR,alarmHour1); EEPROM.put(EEPROM_ALARM1_M_ADDR,alarmMin1);
  EEPROM.put(EEPROM_ALARM2_H_ADDR,alarmHour2); EEPROM.put(EEPROM_ALARM2_M_ADDR,alarmMin2);
  EEPROM.commit();
}

// -------- Web Server Handlers --------
// Fungsi processor tidak lagi digunakan karena JS mengambil nilai awal via /get_alarms
String processor(const String& var){
  return String(); 
}

void handleRoot(){
  server.sendHeader("Content-Type", "text/html; charset=utf-8"); // Memastikan emoji ditampilkan dengan benar
  server.send_P(200, "text/html", htmlPage); 
}

void handleGetAlarms(){
  String json = "{\"h1\":" + String(alarmHour1) + 
                ",\"m1\":" + String(alarmMin1) + 
                ",\"h2\":" + String(alarmHour2) + 
                ",\"m2\":" + String(alarmMin2) + "}";
  server.send(200, "application/json", json);
}

void handleSetAlarm(){
  if (server.hasArg("h1")) alarmHour1 = server.arg("h1").toInt();
  if (server.hasArg("m1")) alarmMin1 = server.arg("m1").toInt();
  if (server.hasArg("h2")) alarmHour2 = server.arg("h2").toInt();
  if (server.hasArg("m2")) alarmMin2 = server.arg("m2").toInt();
  saveAlarms();
  Serial.printf("Alarm 1 set to %02d:%02d\n", alarmHour1, alarmMin1);
  Serial.printf("Alarm 2 set to %02d:%02d\n", alarmHour2, alarmMin2);
  server.send(200,"text/plain","Alarm disimpan!");
}

void handleTimeNow(){
  struct tm *t = localtime(&currentSystemTime);
  char buf[20]; // Cukup untuk YYYY-MM-DD HH:MM:SS
  strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", t);
  server.send(200,"text/plain",buf);
}

void handleSetManual(){
  if (server.hasArg("date") && server.hasArg("time")){
    String date_str = server.arg("date"); // YYYY-MM-DD
    String time_str = server.arg("time"); // HH:MM

    struct tm tm;
    memset(&tm, 0, sizeof(tm)); // Inisialisasi struktur tm dengan nol

    // Gabungkan tanggal dan waktu menjadi satu string untuk strptime yang lebih robust
    String datetime_str = date_str + " " + time_str;

    // Gunakan strptime untuk mengurai string tanggal dan waktu
    char* result = strptime(datetime_str.c_str(), "%Y-%m-%d %H:%M", &tm);

    if (result == nullptr || *result != '\0') {
      server.send(400, "text/plain", "Gagal mengurai tanggal/waktu. Format salah.");
      return;
    }

    tm.tm_isdst = -1; // Biarkan mktime menentukan DST (Daylight Saving Time)
    time_t manual_epoch_time = mktime(&tm);

    if (manual_epoch_time == (time_t)-1) {
      server.send(400, "text/plain", "Gagal mengkonversi ke waktu epoch. Tanggal/waktu tidak valid.");
      return;
    }

    timeval tv = { .tv_sec = manual_epoch_time, .tv_usec = 0 };
    settimeofday(&tv, nullptr); // Set waktu sistem ESP

    // Setelah settimeofday, perbarui currentSystemTime
    currentSystemTime = time(nullptr); 
    
    Serial.print("Waktu manual diset: ");
    Serial.println(ctime(&currentSystemTime));
    server.send(200,"text/plain","Waktu manual berhasil diset!");
  } else {
    server.send(400,"text/plain","Parameter date/time hilang");
  }
}

// Fungsi baru untuk mengganti mode tampilan (Jam/Tanggal)
void toggleDisplayMode() {
  displayTanggal = !displayTanggal;
  Serial.printf("Tampilan diubah menjadi: %s\n", displayTanggal ? "Tanggal" : "Jam");
}

void handleToggleDisplay(){
  toggleDisplayMode(); // Panggil fungsi yang sama
  server.send(200,"text/plain", String("Tampilan: ") + (displayTanggal?"Tanggal":"Jam"));
}

void handleDisableAP(){
  if (WiFi.getMode() == WIFI_AP) {
    WiFi.softAPdisconnect(true);
    server.send(200,"text/plain","Hotspot dimatikan. Mungkin perlu restart untuk masuk mode STA.");
    delay(100); // Beri waktu server mengirim respons sebelum memutuskan
    ESP.restart(); // Restart untuk memastikan mode STA aktif jika ada koneksi
  } else {
    server.send(200, "text/plain", "Hotspot tidak aktif.");
  }
}

void handleSyncNTP() {
  timeClient.forceUpdate(); // Paksa update NTP
  currentSystemTime = timeClient.getEpochTime();
  Serial.print("Forced NTP sync. Current time: ");
  Serial.println(ctime(&currentSystemTime));
  server.send(200, "text/plain", "Sinkronisasi NTP berhasil!");
}

// FUNGSI BARU UNTUK STATUS JARINGAN
void handleNetworkStatus() {
  String json = "{";
  json += "\"wifiStatus\": \"" + String(WiFi.status() == WL_CONNECTED ? "Terhubung" : "Tidak Terhubung") + "\",";
  json += "\"wifiSSID\": \"" + (WiFi.status() == WL_CONNECTED ? WiFi.SSID() : "N/A") + "\",";
  json += "\"localIP\": \"" + WiFi.localIP().toString() + "\",";
  json += "\"apStatus\": \"" + String(WiFi.getMode() == WIFI_AP ? "Aktif" : "Mati") + "\",";
  json += "\"apIP\": \"" + WiFi.softAPIP().toString() + "\"";
  json += "}";
  server.send(200, "application/json", json);
}

void setupWeb(){
  server.on("/", handleRoot);
  server.on("/get_alarms", handleGetAlarms); // Endpoint baru untuk mengambil nilai alarm awal
  server.on("/set_alarm", handleSetAlarm);
  server.on("/time", handleTimeNow);
  server.on("/set_manual", handleSetManual);
  server.on("/toggle_display", handleToggleDisplay);
  server.on("/disable_ap", handleDisableAP);
  server.on("/sync_ntp", handleSyncNTP); // Endpoint baru untuk sinkronisasi NTP manual
  server.on("/network_status", handleNetworkStatus); // DAFTARKAN ENDPOINT BARU DI SINI
  server.begin();
  Serial.println("Web server started.");
}

// ------ Logic Functions -------
void alarmCheck(){
  struct tm *t = localtime(&currentSystemTime);
  int h = t->tm_hour, m = t->tm_min;
  
  bool trg = ((h==alarmHour1 && m==alarmMin1) || (h==alarmHour2 && m==alarmMin2));
  static bool lastTrig = false; // Memastikan alarm hanya aktif sekali per pemicuan
  if (trg && !lastTrig){
    alarmAktif=true; 
    alarmStartTime=millis(); 
    alarmLastStepTime=millis(); 
    alarmStep=0; // Reset alarm step
    Serial.println("Alarm triggered!");
  }
  lastTrig = trg;
}

void updateAlarm(){
  if (!alarmAktif) return;
  
  if (millis()-alarmStartTime > 60000){ // Alarm aktif selama 60 detik
    alarmAktif=false;
    noTone(buzzer); digitalWrite(led1,LOW); digitalWrite(led2,LOW);
    Serial.println("Alarm finished.");
    return;
  }
  
  if (millis()-alarmLastStepTime < 200) return; // Setiap 200ms
  alarmLastStepTime = millis();
  
  digitalWrite(led1, alarmStep==0 || alarmStep==4);
  digitalWrite(led2, alarmStep==2 || alarmStep==4); // LED2 juga ikut pola alarm
  
  int tones[] = {500,0,1000,0,1500,0,2000}; // Pola nada
  if (tones[alarmStep]) {
    tone(buzzer, tones[alarmStep]); 
  } else {
    noTone(buzzer);
  }
  alarmStep = (alarmStep+1)%7;
}

// Fungsi untuk memeriksa dan mengaktifkan efek "jam tepat" (chime hourly)
void checkJamTepat(){
  struct tm *t = localtime(&currentSystemTime);
  // Pemicu jam tepat hanya di menit 00, dan tidak aktif jika sudah aktif,
  // dan sudah lebih dari 60 detik sejak jam tepat terakhir aktif (untuk menghindari re-trigger)
  if (t->tm_min==0 && !jamTepatActive && (millis()-lastJamTepatMillis > 60000)){
    jamTepatActive=true;
    jamTepatStart=millis();
    alarmLastStepTime = millis(); // Reset waktu langkah untuk kontrol pola bunyi
    alarmStep = 0; // Reset step untuk pola bunyi baru
    Serial.println("Jam Tepat triggered!");
    lastJamTepatMillis = millis(); // Update waktu terakhir pemicuan
  }

  if (jamTepatActive){
    // Pola bunyi dan LED setiap 300ms
    if (millis() - alarmLastStepTime >= 300) { // Durasi setiap nada/langkah = 300ms
      alarmLastStepTime = millis(); // Perbarui waktu langkah terakhir
      
      // Pola nada: 600Hz, 1000Hz, 750Hz, kemudian senyap
      // Tambahkan '0' lebih banyak di akhir untuk memastikan tidak ada nada yang salah diakses
      int tones[] = {600, 1000, 750, 0, 0}; // 3 nada bunyi, diikuti 2 jeda
      
      // Pastikan alarmStep tidak melebihi ukuran array tones
      if (alarmStep < sizeof(tones)/sizeof(tones[0])) {
          if (tones[alarmStep]) { // Jika ada nada, bunyikan dan nyalakan LED
              tone(buzzer, tones[alarmStep]);
              if (alarmStep == 0) { // Nada pertama (600Hz)
                  digitalWrite(led1, HIGH);
                  digitalWrite(led2, LOW);
              } else if (alarmStep == 1) { // Nada kedua (1000Hz)
                  digitalWrite(led1, LOW);
                  digitalWrite(led2, HIGH);
              } else if (alarmStep == 2) { // Nada ketiga (750Hz)
                  digitalWrite(led1, HIGH); 
                  digitalWrite(led2, HIGH); 
              }
          } else { // Jika nilai nada nol (jeda), matikan buzzer dan kedua LED
              noTone(buzzer);
              digitalWrite(led1, LOW);
              digitalWrite(led2, LOW);
              // Karena sudah tidak ada nada lagi, kita bisa langsung menonaktifkan jam tepat
              jamTepatActive = false; // Nonaktifkan jam tepat setelah pola bunyi selesai
              Serial.println("Jam Tepat finished.");
          }
      } else { // Jika sudah melewati semua nada yang ditentukan (setelah nada 750Hz dan jeda)
          noTone(buzzer);
          digitalWrite(led1, LOW);
          digitalWrite(led2, LOW);
          jamTepatActive = false; // Nonaktifkan jam tepat
          Serial.println("Jam Tepat finished.");
      }
      
      alarmStep++; // Lanjut ke langkah pola bunyi dan LED berikutnya
    }
  }
}

// Fungsi untuk menangani buzzer setiap pergantian menit
void handleMinuteChange(){
  struct tm *t_current = localtime(&currentSystemTime);
  int currentMinute = t_current->tm_min;

  // Cek apakah menit berubah
  if (currentMinute != lastMinute) {
    // Memastikan perubahan adalah 1 menit maju
    if (currentMinute == (lastMinute + 1) % 60 || (lastMinute == 59 && currentMinute == 0)) { 
        // Hanya picu efek jika tidak ada alarm atau jam tepat aktif
        if (!jamTepatActive && !alarmAktif) {
            minuteChangeActive = true;
            minuteChangeStartTime = millis();
            // digitalWrite(led2, HIGH); // LED2 TIDAK dinyalakan di sini
            tone(buzzer, 750); // Frekuensi 750Hz
            Serial.printf("Minute changed to %02d. Buzzer (750Hz) active.\n", currentMinute);
        }
    }
    lastMinute = currentMinute; // Update menit terakhir
  }

  // Matikan buzzer setelah durasi singkat (200ms)
  if (minuteChangeActive && (millis() - minuteChangeStartTime >= 200)) { 
    minuteChangeActive = false;
    // Matikan hanya jika tidak ada alarm atau jam tepat yang aktif
    if (!jamTepatActive && !alarmAktif) {
        noTone(buzzer);
        // digitalWrite(led2, LOW); // LED2 TIDAK dimatikan di sini
    }
    Serial.println("Minute change effect finished.");
  }
}

void scrollDisplay(){
  if (!scrollingActive) return;
  
  // Aktifkan LED2 sesaat saat scrolling dimulai, jika tidak ada alarm/jam tepat aktif,
  // dan LED2 pulse belum aktif
  if (scrollStep == 0 && !jamTepatActive && !alarmAktif && !led2PulseActive) {
      digitalWrite(led2, HIGH); // Nyalakan LED2
      led2PulseStartTime = millis(); // Catat waktu mulai pulse
      led2PulseActive = true; // Set flag bahwa pulse aktif
      Serial.println("LED2 pulse for scrolling activated.");
  }
  
  // LOGIC BARU: Matikan LED2 setelah 200ms jika pulse aktif dan belum ada alarm/jam tepat
  if (led2PulseActive && (millis() - led2PulseStartTime >= LED2_PULSE_DURATION)) {
      if (!jamTepatActive && !alarmAktif) { // Pastikan tidak mengganggu yang lain
          digitalWrite(led2, LOW);
      }
      led2PulseActive = false; // Matikan flag pulse
      Serial.println("LED2 pulse for scrolling deactivated.");
  }
  
  // Jika display.displayAnimate() mengembalikan true, berarti animasi sebelumnya selesai
  // dan siap untuk teks berikutnya
  if (display.displayAnimate()){
    scrollStep++;
    char buf[64]; // Variabel buf dideklarasikan di sini agar lingkupnya hanya dalam if (display.displayAnimate())
    switch(scrollStep){
      case 1: { // Kurung kurawal untuk lingkup variabel
        struct tm *t_scroll = localtime(&currentSystemTime);
        snprintf(buf,sizeof(buf),"Tanggal: %02d-%02d-%04d",
          t_scroll->tm_mday, t_scroll->tm_mon+1, t_scroll->tm_year+1900);
        break;
      } 
      case 2: {
        if (WiFi.status() == WL_CONNECTED) {
          snprintf(buf,sizeof(buf),"WiFi: %s", WiFi.SSID().c_str());
        } else {
          strcpy(buf, "WiFi: Disconnected");
        }
        break;
      }
      case 3: {
        snprintf(buf,sizeof(buf),"Hotspot: %s", WiFi.getMode()==WIFI_AP?"AKTIF":"Mati");
        break;
      }
      case 4: {
        snprintf(buf,sizeof(buf),"IP: %s", WiFi.localIP().toString().c_str());
        break;
      }
      case 5: {
        snprintf(buf,sizeof(buf),"Alarm1: %02d:%02d", alarmHour1, alarmMin1);
        break;
      }
      case 6: {
        snprintf(buf,sizeof(buf),"Alarm2: %02d:%02d", alarmHour2, alarmMin2);
        break;
      }
      default: // Selesai scrolling, reset
        scrollingActive=false;
        scrollStep=0;
        // Tidak perlu matikan LED2 di sini lagi karena sudah dihandle oleh led2PulseActive
        return;
    }
    // Set teks untuk di-scroll pada display
    display.displayScroll(buf, PA_LEFT, PA_SCROLL_LEFT, 100);
  }
}
  
void loop(){
  server.handleClient();
  
  // Perbarui waktu dari NTP client secara berkala
  timeClient.update();
  currentSystemTime = timeClient.getEpochTime(); // Dapatkan waktu terbaru
  
  // --- Panggil Fungsi Logic Perangkat ---
  alarmCheck();
  updateAlarm();
  checkJamTepat();      // Panggil ini pertama
  handleMinuteChange(); // Panggil setelah checkJamTepat, agar jam tepat punya prioritas

  // --- Logika Deteksi Tombol D4 (Fitur Baru) ---
  int currentButtonState = digitalRead(BUTTON_PIN);

  // Debounce logic
  if (currentButtonState != lastButtonState) {
    lastButtonPressTime = millis();
  }

  if ((millis() - lastButtonPressTime) > DEBOUNCE_DELAY) {
    // Hanya proses jika status tombol sudah stabil
    if (currentButtonState != lastButtonState) {
      lastButtonState = currentButtonState;

      if (currentButtonState == LOW) { // Tombol ditekan (transisi HIGH ke LOW)
        Serial.println("Tombol D4 ditekan!");
        toggleDisplayMode(); // Panggil fungsi untuk mengganti tampilan
      }
    }
  }
  // --- Akhir Logika Deteksi Tombol D4 ---


  // Update display utama (jam atau tanggal)
  // Hanya update display utama jika tidak ada scrolling aktif
  if (!scrollingActive) {
    display.displayClear(); // Hapus tampilan sebelumnya
    struct tm *t = localtime(&currentSystemTime); // Gunakan waktu sistem terbaru

    char tbuf[25]; // Buffer cukup besar untuk tanggal dan waktu (contoh: 22-06-2025 18:50)
    if (displayTanggal){
      // Format tanggal: DD-MM-YYYY HH:MM
      // strftime hanya butuh pointer ke struct tm sebagai argumen terakhir
      strftime(tbuf,sizeof(tbuf),"%02d-%02d-%04d %02d:%02d", t); 
    } else {
      // Format jam: HH:MM:SS
      // strftime hanya butuh pointer ke struct tm sebagai argumen terakhir
      strftime(tbuf,sizeof(tbuf),"%02d:%02d:%02d", t); 
    }
    display.displayText(tbuf, PA_CENTER, 0, 0, PA_PRINT, PA_NO_EFFECT);
  }
  display.displayAnimate(); // Penting untuk semua operasi display Parola

  // Trigger scrolling setiap 10 menit
  // Pastikan scrolling hanya dipicu pada awal menit kelipatan 10 (misal 10:00, 10:10, dll.)
  static int lastMinForScroll = -1; // Inisialisasi statis
  struct tm *t_current = localtime(&currentSystemTime);
  int cm = t_current->tm_min;

  // Hanya trigger scrolling jika menit adalah kelipatan 10 DAN itu adalah perubahan menit baru
  // Ini memastikan LED2 menyala hanya pada awal detik ke-00 dari menit kelipatan 10
  if (cm % 10 == 0 && cm != lastMinForScroll && t_current->tm_sec == 0){ // Hanya di detik ke-00
    scrollingActive = true;
    scrollStep = 0; // Reset scroll step untuk memulai dari awal
    lastMinForScroll = cm;
    Serial.println("Scrolling info triggered.");
    // LED2 akan menyala di awal scrollDisplay() jika tidak ada interupsi lain
  }

  scrollDisplay(); // Jalankan fungsi scrolling jika aktif
}
  
void setup(){
  Serial.begin(115200);
  setupPins();
  setupDisplay();
  connectWiFi(); // Menggunakan fungsi connectWiFi yang lebih baik
  setupNTP();
  setupEEPROM();
  setupWeb();
  
  // Tampilkan pesan awal pada display setelah setup selesai
  display.displayClear();
  display.displayText("READY", PA_CENTER, 100, 0, PA_PRINT, PA_NO_EFFECT);
  delay(1000); // Beri waktu display untuk menampilkan "READY"

  // Aktifkan scrolling awal setelah startup
  scrollingActive = true;
  scrollStep = 0;
}
