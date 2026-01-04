/* CODIGO RECEPTOR V41 - ULTIMATE COLD BOOT FIX (PIN 36) */
#include "LoRaWan_APP.h"
#include <WiFi.h>
#include <WebServer.h>
#include <Wire.h>
#include "HT_SSD1306Wire.h"
#include <Preferences.h>
#include <Update.h>

#define PRG_BUTTON 0

// --- PINES ---
#define SDA_OLED 17
#define SCL_OLED 18
#define RST_OLED 21
#define Vext 36      // <--- CORRECTO: Pin 36
#define LED_PIN 35   
// -------------

#define LORA_SCK 9
#define LORA_MISO 11
#define LORA_MOSI 10
#define LORA_CS 8
#define RF_FREQUENCY 868100000

SSD1306Wire screen(0x3c, 500000, SDA_OLED, SCL_OLED, GEOMETRY_128_64, RST_OLED);
WebServer server(80);
Preferences preferences;
static RadioEvents_t RadioEvents;

int rx_rssi_lora=0; unsigned long last_packet=0; bool signal_lost=false;
int p_perc=0; int p_time=0; String p_stat="ESPERANDO"; int p_noz=0; int p_bed=0;
int lora_profile=2; int lora_power=14;
String wifi_sta_ssid=""; String wifi_sta_pass=""; String wifi_ap_pass=""; 
unsigned long btn_press_start = 0; bool btn_pressed = false;

String getValue(String d, char s, int i) {
    int f=0; int str[]={0,-1}; int m=d.length()-1;
    for(int j=0; j<=m && f<=i; j++) { if(d.charAt(j)==s || j==m) { f++; str[0]=str[1]+1; str[1]=(j==m)?j+1:j; } }
    return f>i ? d.substring(str[0], str[1]) : "";
}

void updateDisplay() {
    screen.clear(); 
    screen.setFont(ArialMT_Plain_10); 
    screen.setTextAlignment(TEXT_ALIGN_LEFT); screen.drawString(0, 0, (WiFi.status()==WL_CONNECTED)?WiFi.localIP().toString():"AP:192.168.4.1");
    screen.setTextAlignment(TEXT_ALIGN_RIGHT); screen.drawString(128, 0, String(rx_rssi_lora)+"dB");

    if(signal_lost) {
        if((millis()/500)%2==0) {
            screen.setTextAlignment(TEXT_ALIGN_CENTER); screen.setFont(ArialMT_Plain_16);
            screen.drawString(64, 25, "¡ENLACE CAIDO!");
        }
    } else {
        screen.setTextAlignment(TEXT_ALIGN_LEFT); screen.setFont(ArialMT_Plain_24);
        screen.drawString(0, 16, String(p_perc) + "%");
        screen.setFont(ArialMT_Plain_10);
        screen.drawString(58, 16, p_stat.substring(0, 9)); 
        screen.drawString(58, 28, String(p_time) + "m rest.");
        screen.drawLine(0, 46, 128, 46);
        screen.drawString(0, 49, "Nozzle:"+String(p_noz)+" Bed:"+String(p_bed));
    }
    screen.display();
}

void configLoRa() {
    int sf = 9; int bw = 0; 
    switch(lora_profile) { case 0: sf=7; bw=2; break; case 1: sf=7; bw=0; break; case 2: sf=9; bw=0; break; case 3: sf=12; bw=0; break; }
    Radio.SetTxConfig(MODEM_LORA, lora_power, 0, bw, sf, 1, 8, false, true, 0, 0, false, 3000);
    Radio.SetRxConfig(MODEM_LORA, bw, sf, 1, 0, 8, 0, false, 0, true, 0, 0, false, true);
    Radio.Rx(0);
}

void sendCommand(String cmd) {
    screen.clear(); screen.setFont(ArialMT_Plain_16); screen.setTextAlignment(TEXT_ALIGN_CENTER);
    screen.drawString(64, 20, "ENVIANDO..."); screen.display();
    Radio.Send((uint8_t*)cmd.c_str(), cmd.length());
}

void handleCommand() {
    if(server.hasArg("act")) { sendCommand("ACT:"+server.arg("act")); server.send(200, "text/plain", "OK"); } 
    else if(server.hasArg("gcode")) { sendCommand("GCODE:"+server.arg("gcode")); server.send(200, "text/plain", "OK"); }
    else if(server.hasArg("file")) { sendCommand("FILE:"+server.arg("file")); server.send(200, "text/plain", "OK"); }
    else server.send(400, "text/plain", "Error");
}

String getHtml() {
  String h = "<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1'><style>body{background:#111;color:#eee;font-family:sans-serif;text-align:center;}input,button{width:100%;padding:10px;margin:5px 0;}</style><script>setInterval(()=>{fetch('/data').then(r=>r.json()).then(d=>{document.getElementById('p').innerText=d.p+'%';document.getElementById('s').innerText=d.s;})},2000);function c(u){fetch(u);}</script></head><body><h2>RECEPTOR V41</h2>";
  h += "<h1><span id='p'>"+String(p_perc)+"%</span></h1><p id='s'>"+p_stat+"</p>";
  h += "<input type='text' id='f' placeholder='archivo.gcode'><button onclick=\"c('/cmd?file='+document.getElementById('f').value)\">IMPRIMIR</button>";
  h += "<input type='text' id='g' placeholder='G28'><button onclick=\"c('/cmd?gcode='+document.getElementById('g').value)\">G-CODE</button>";
  h += "<button onclick=\"c('/cmd?act=PAUSE')\" style='background:#e3b341;color:black'>PAUSA</button><button onclick=\"c('/cmd?act=RESUME')\" style='background:#0a0'>PLAY</button><button onclick=\"c('/cmd?act=STOP')\" style='background:#d32f2f'>STOP</button>";
  h += "</body></html>";
  return h;
}

void handleUpdate() {
  HTTPUpload& upload = server.upload();
  if (upload.status == UPLOAD_FILE_START) Update.begin(UPDATE_SIZE_UNKNOWN);
  else if (upload.status == UPLOAD_FILE_WRITE) Update.write(upload.buf, upload.currentSize);
  else if (upload.status == UPLOAD_FILE_END) { Update.end(true); server.send(200,"text/html","<h1>OK</h1>"); delay(1000); ESP.restart(); }
}

// ================= SETUP BLINDADO =================
void setup() {
    Serial.begin(115200);
    pinMode(LED_PIN, OUTPUT); digitalWrite(LED_PIN, HIGH);
    pinMode(PRG_BUTTON, INPUT_PULLUP);
    
    preferences.begin("conf", false);
    lora_profile = preferences.getInt("prof", 2); lora_power = preferences.getInt("pow", 14);
    wifi_sta_ssid = preferences.getString("ssid", ""); wifi_sta_pass = preferences.getString("pass", ""); wifi_ap_pass = preferences.getString("appass", "");
    preferences.end();

    // 1. ESPERA INICIAL
    delay(1000);

    // 2. CONFIG PINES
    pinMode(Vext, OUTPUT);
    pinMode(RST_OLED, OUTPUT);
    
    // 3. SECUENCIA FUERZA BRUTA
    // Ciclo 1
    digitalWrite(Vext, HIGH); delay(300);
    digitalWrite(Vext, LOW);  delay(500);
    // Ciclo 2
    digitalWrite(Vext, HIGH); delay(300);
    digitalWrite(Vext, LOW);  delay(500);

    // Reset Doble
    digitalWrite(RST_OLED, LOW);  delay(300);
    digitalWrite(RST_OLED, HIGH); delay(300);
    digitalWrite(RST_OLED, LOW);  delay(300);
    digitalWrite(RST_OLED, HIGH); delay(300);
    
    // 4. INICIO
    screen.init();

    // 5. REINTENTO
    if (screen.getStringWidth("T") == 0) { 
        Serial.println("WARN: OLED fallo 1. Reintentando...");
        digitalWrite(Vext, HIGH); delay(200);
        digitalWrite(Vext, LOW);  delay(500);
        digitalWrite(RST_OLED, LOW); delay(500);
        digitalWrite(RST_OLED, HIGH); delay(500);
        screen.init();
    }
    
    screen.flipScreenVertically(); screen.setFont(ArialMT_Plain_10);
    // ================= FIN SETUP =================
    
    WiFi.mode(WIFI_AP_STA);
    if(wifi_ap_pass == "") WiFi.softAP("HP_Receptor", NULL); else WiFi.softAP("HP_Receptor", wifi_ap_pass.c_str());
    if(wifi_sta_ssid != "") WiFi.begin(wifi_sta_ssid.c_str(), wifi_sta_pass.c_str());

    server.on("/", [](){ server.send(200, "text/html", getHtml()); });
    server.on("/data", [](){ String j="{\"p\":"+String(p_perc)+",\"s\":\""+p_stat+"\"}"; server.send(200,"application/json",j); });
    server.on("/cmd", handleCommand);
    server.on("/update", HTTP_POST, [](){ server.send(200, "text/plain", (Update.hasError())?"FAIL":"OK"); }, handleUpdate);
    server.begin();

    SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_CS);
    RadioEvents.TxDone = [](){ Radio.Rx(0); }; 
    RadioEvents.RxDone = [](uint8_t *payload, uint16_t size, int16_t rssi, int8_t snr){
        rx_rssi_lora = rssi; last_packet = millis(); signal_lost=false;
        char msg[size+1]; memcpy(msg, payload, size); msg[size]=0; String d(msg);
        if(d.indexOf('|')>0) {
            p_perc=getValue(d,'|',0).toInt(); p_time=getValue(d,'|',1).toInt(); p_stat=getValue(d,'|',2);
            p_noz=getValue(d,'|',3).toInt(); p_bed=getValue(d,'|',4).toInt();
            updateDisplay();
            // Latido LED
            digitalWrite(LED_PIN, HIGH); delay(50); digitalWrite(LED_PIN, LOW);
        }
        Radio.Rx(0);
    };
    Radio.Init(&RadioEvents); Radio.SetChannel(RF_FREQUENCY);
    configLoRa();
    updateDisplay();
    digitalWrite(LED_PIN, LOW);
}

void loop() {
    server.handleClient();
    Radio.IrqProcess();
    if(millis()-last_packet > 30000 && !signal_lost) { signal_lost=true; updateDisplay(); }

    int btnState = digitalRead(PRG_BUTTON);
    if (btnState == LOW && !btn_pressed) { btn_pressed = true; btn_press_start = millis(); }
    if (btnState == HIGH && btn_pressed) {
        unsigned long duration = millis() - btn_press_start; btn_pressed = false;
        if (duration < 2000) sendCommand("ACT:PAUSE"); 
        else sendCommand("ACT:STOP");
    }
}
