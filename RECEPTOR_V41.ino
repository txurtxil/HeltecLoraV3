/* CODIGO RECEPTOR V44 - GPAD XYZ + SEÑALES WEB + OLED FIX */
#include "LoRaWan_APP.h"
#include <WiFi.h>
#include <WebServer.h>
#include <Wire.h>
#include "HT_SSD1306Wire.h"
#include <Preferences.h>
#include <Update.h>

#define PRG_BUTTON 0

// --- PINES HELTEC V3 ---
#define SDA_OLED 17
#define SCL_OLED 18
#define RST_OLED 21
#define Vext 36      
#define LED_PIN 35   

#define LORA_SCK 9
#define LORA_MISO 11
#define LORA_MOSI 10
#define LORA_CS 8
#define RF_FREQUENCY 868100000

SSD1306Wire screen(0x3c, 500000, SDA_OLED, SCL_OLED, GEOMETRY_128_64, RST_OLED);
WebServer server(80);
Preferences preferences;
static RadioEvents_t RadioEvents;

// VARIABLES
int rx_rssi_lora=0; 
long wifi_rssi=0;
unsigned long last_packet=0; 
bool signal_lost=false;

// ESTADO IMPRESORA
int p_perc=0; int p_time=0; String p_stat="ESPERANDO"; 
int p_noz=0; int p_bed=0;

// CONFIG
int lora_profile=2; int lora_power=14;
String wifi_sta_ssid=""; String wifi_sta_pass=""; String wifi_ap_pass=""; 
unsigned long btn_press_start = 0; bool btn_pressed = false;

String getValue(String d, char s, int i) {
    int f=0; int str[]={0,-1}; int m=d.length()-1;
    for(int j=0; j<=m && f<=i; j++) { if(d.charAt(j)==s || j==m) { f++; str[0]=str[1]+1; str[1]=(j==m)?j+1:j; } }
    return f>i ? d.substring(str[0], str[1]) : "";
}

// --- PANTALLA OLED ---
void updateDisplay() {
    screen.clear(); 
    screen.setFont(ArialMT_Plain_10); 
    
    // 1. IP (Izquierda)
    screen.setTextAlignment(TEXT_ALIGN_LEFT); 
    if(WiFi.status() == WL_CONNECTED) {
        screen.drawString(0, 0, WiFi.localIP().toString());
        wifi_rssi = WiFi.RSSI();
    } else {
        screen.drawString(0, 0, "AP: 192.168.4.1");
        wifi_rssi = 0;
    }

    // 2. SEÑALES (Derecha) - Ajustadas para que no se corten
    screen.setTextAlignment(TEXT_ALIGN_RIGHT); 
    String sig = "L:" + String(rx_rssi_lora);
    // Si hay WiFi mostramos señal, si no "--"
    if(wifi_rssi != 0) sig += " W:" + String(wifi_rssi); else sig += " W:--";
    screen.drawString(128, 0, sig);

    // 3. DATOS CENTRALES
    if(signal_lost) {
        if((millis()/500)%2==0) {
            screen.setTextAlignment(TEXT_ALIGN_CENTER); 
            screen.setFont(ArialMT_Plain_16);
            screen.drawString(64, 25, "¡SIN SEÑAL!");
            screen.setFont(ArialMT_Plain_10);
            screen.drawString(64, 45, "Revisar Trastero");
        }
    } else {
        screen.setTextAlignment(TEXT_ALIGN_LEFT); 
        screen.setFont(ArialMT_Plain_24);
        screen.drawString(0, 16, String(p_perc) + "%");
        
        screen.setFont(ArialMT_Plain_10);
        screen.drawString(60, 16, p_stat.substring(0, 10)); 
        screen.drawString(60, 28, String(p_time) + " min rest.");
        
        screen.drawLine(0, 46, 128, 46);
        
        // TEMPERATURAS
        String temps = "N:" + String(p_noz) + " B:" + String(p_bed);
        screen.drawString(0, 49, temps);
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
    digitalWrite(LED_PIN, HIGH); delay(100); digitalWrite(LED_PIN, LOW);
}

// --- WEB SERVER HANDLERS ---
void handleCommand() {
    if(server.hasArg("act")) { sendCommand("ACT:"+server.arg("act")); server.send(200, "text/plain", "OK"); } 
    else if(server.hasArg("gcode")) { sendCommand("GCODE:"+server.arg("gcode")); server.send(200, "text/plain", "OK"); }
    else if(server.hasArg("file")) { sendCommand("FILE:"+server.arg("file")); server.send(200, "text/plain", "OK"); }
    else server.send(400, "text/plain", "Error");
}

void handleSaveWiFi() {
    if(server.hasArg("ssid") && server.hasArg("pass")) {
        preferences.begin("conf", false);
        preferences.putString("ssid", server.arg("ssid"));
        preferences.putString("pass", server.arg("pass"));
        preferences.end();
        server.send(200, "text/html", "<h1>Guardado. Reiniciando...</h1>");
        delay(1000); ESP.restart();
    } else server.send(400, "text/plain", "Error datos");
}

void handleUpdate() {
  HTTPUpload& upload = server.upload();
  if (upload.status == UPLOAD_FILE_START) Update.begin(UPDATE_SIZE_UNKNOWN);
  else if (upload.status == UPLOAD_FILE_WRITE) Update.write(upload.buf, upload.currentSize);
  else if (upload.status == UPLOAD_FILE_END) { Update.end(true); server.send(200,"text/html","<h1>OK ACTUALIZADO</h1>"); delay(1000); ESP.restart(); }
}

// --- HTML CON G-PAD Y SEÑALES ---
String getHtml() {
  String h = "<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1'>";
  h += "<style>";
  h += "body{background:#1a1a1a;color:#eee;font-family:sans-serif;text-align:center;margin:0;padding:5px;}";
  h += ".card{background:#2a2a2a;padding:10px;margin:10px auto;border-radius:12px;max-width:400px;border:1px solid #444;}";
  h += "h1{margin:0;font-size:35px;color:#00d2ff;} h3{border-bottom:1px solid #555;padding-bottom:5px;color:#aaa;}";
  h += ".sig-bar{font-size:12px;background:#111;padding:5px;border-radius:5px;margin-bottom:10px;color:#0f0;}";
  h += "input{width:65%;padding:8px;background:#111;border:1px solid #555;color:white;border-radius:5px;}";
  h += "button{padding:8px 15px;border:none;border-radius:5px;color:white;font-weight:bold;cursor:pointer;margin:2px;}";
  h += ".btn-blue{background:#007bff;} .btn-green{background:#28a745;} .btn-red{background:#dc3545;} .btn-yell{background:#ffc107;color:black;} .btn-gray{background:#555;}";
  h += ".gpad-grid{display:grid;grid-template-columns:1fr 1fr 1fr;gap:5px;max-width:200px;margin:0 auto;}";
  h += ".z-grid{display:grid;grid-template-columns:1fr 1fr;gap:5px;max-width:200px;margin:10px auto;}";
  h += "</style>";
  
  // Script para actualizar datos y señales
  h += "<script>";
  h += "setInterval(()=>{fetch('/data').then(r=>r.json()).then(d=>{";
  h += "document.getElementById('p').innerText=d.p+'%';";
  h += "document.getElementById('s').innerText=d.s;";
  h += "document.getElementById('sig').innerText='LoRa: '+d.l+'dBm | WiFi: '+d.w+'dBm';";
  h += "})},2000);";
  h += "function c(u){fetch(u);}";
  h += "</script>";
  
  h += "</head><body><h2>🛸 RECEPTOR V44</h2>";
  
  // BARRA DE SEÑALES WEB
  h += "<div class='sig-bar' id='sig'>Cargando señales...</div>";
  
  // ESTADO
  h += "<div class='card'><h1 id='p'>"+String(p_perc)+"%</h1><p id='s'>"+p_stat+"</p>";
  h += "<p>🌡️ Nozzle: "+String(p_noz)+"°C | Bed: "+String(p_bed)+"°C</p></div>";
  
  // G-PAD (MOVIMIENTO XYZ + HOME)
  h += "<div class='card'><h3>🕹️ MOVIMIENTO (G-PAD)</h3>";
  // Boton Home G28
  h += "<button class='btn-yell' style='width:100%;margin-bottom:10px' onclick=\"c('/cmd?gcode=G28')\">🏠 HOME ALL (G28)</button>";
  // Cruz XY
  h += "<div class='gpad-grid'>";
  h += "<div></div><button class='btn-gray' onclick=\"c('/cmd?gcode=G91 G1 Y10 F3000 G90')\">⬆️ Y+</button><div></div>";
  h += "<button class='btn-gray' onclick=\"c('/cmd?gcode=G91 G1 X-10 F3000 G90')\">⬅️ X-</button>";
  h += "<button class='btn-blue' onclick=\"c('/cmd?gcode=G90')\">🎯 ABS</button>"; // Boton centro reset modo
  h += "<button class='btn-gray' onclick=\"c('/cmd?gcode=G91 G1 X10 F3000 G90')\">➡️ X+</button>";
  h += "<div></div><button class='btn-gray' onclick=\"c('/cmd?gcode=G91 G1 Y-10 F3000 G90')\">⬇️ Y-</button><div></div>";
  h += "</div>";
  // Eje Z
  h += "<div class='z-grid'>";
  h += "<button class='btn-gray' onclick=\"c('/cmd?gcode=G91 G1 Z10 F600 G90')\">⏫ Z+ (Subir)</button>";
  h += "<button class='btn-gray' onclick=\"c('/cmd?gcode=G91 G1 Z-10 F600 G90')\">⏬ Z- (Bajar)</button>";
  h += "</div></div>";

  // CONTROL BASICO
  h += "<div class='card'><h3>⏯ CONTROL</h3>";
  h += "<button class='btn-yell' onclick=\"c('/cmd?act=PAUSE')\">PAUSA</button>";
  h += "<button class='btn-green' onclick=\"c('/cmd?act=RESUME')\">PLAY</button>";
  h += "<button class='btn-red' onclick=\"c('/cmd?act=STOP')\">STOP</button></div>";

  // CONSOLA Y ARCHIVOS
  h += "<div class='card'><h3>📂 ARCHIVOS / GCODE</h3>";
  h += "<input type='text' id='f' placeholder='archivo.gcode'><button class='btn-blue' onclick=\"c('/cmd?file='+document.getElementById('f').value)\">🖨️</button><br>";
  h += "<input type='text' id='g' placeholder='G28' style='margin-top:5px'><button class='btn-blue' onclick=\"c('/cmd?gcode='+document.getElementById('g').value)\">📩</button></div>";

  // CONFIG Y OTA
  h += "<div class='card'><h3>⚙️ WIFI / OTA</h3>";
  h += "<form action='/wifi' method='POST' style='display:inline'><input type='text' name='ssid' placeholder='SSID' value='"+wifi_sta_ssid+"'><input type='password' name='pass' placeholder='Pass'><button class='btn-green'>Conectar</button></form><br>";
  h += "<form method='POST' action='/update' enctype='multipart/form-data' style='margin-top:10px'><input type='file' name='update' style='width:60%'><button class='btn-yell'>Subir .BIN</button></form></div>";

  h += "</body></html>";
  return h;
}

// ================= SETUP =================
void setup() {
    Serial.begin(115200);
    pinMode(LED_PIN, OUTPUT); digitalWrite(LED_PIN, HIGH);
    pinMode(PRG_BUTTON, INPUT_PULLUP);
    
    preferences.begin("conf", false);
    lora_profile = preferences.getInt("prof", 2); 
    lora_power = preferences.getInt("pow", 14);
    wifi_sta_ssid = preferences.getString("ssid", ""); 
    wifi_sta_pass = preferences.getString("pass", ""); 
    wifi_ap_pass = preferences.getString("appass", "");
    preferences.end();

    delay(1000);

    // --- FIX OLED V41 ---
    pinMode(Vext, OUTPUT); pinMode(RST_OLED, OUTPUT);
    digitalWrite(Vext, HIGH); delay(300); digitalWrite(Vext, LOW); delay(500);
    digitalWrite(Vext, HIGH); delay(300); digitalWrite(Vext, LOW); delay(500);
    digitalWrite(RST_OLED, LOW); delay(300); digitalWrite(RST_OLED, HIGH); delay(300);
    digitalWrite(RST_OLED, LOW); delay(300); digitalWrite(RST_OLED, HIGH); delay(300);
    
    screen.init();
    if (screen.getStringWidth("T") == 0) { 
        Serial.println("Reintentando OLED...");
        digitalWrite(Vext, HIGH); delay(200); digitalWrite(Vext, LOW); delay(500);
        digitalWrite(RST_OLED, LOW); delay(500); digitalWrite(RST_OLED, HIGH); delay(500);
        screen.init();
    }
    screen.flipScreenVertically(); screen.setFont(ArialMT_Plain_10);
    screen.clear(); screen.drawString(0,0,"INICIANDO V44..."); screen.display();
    // --------------------

    WiFi.mode(WIFI_AP_STA);
    if(wifi_ap_pass == "") WiFi.softAP("HP_Receptor", NULL); else WiFi.softAP("HP_Receptor", wifi_ap_pass.c_str());
    if(wifi_sta_ssid != "") WiFi.begin(wifi_sta_ssid.c_str(), wifi_sta_pass.c_str());

    // ENDPOINTS WEB
    server.on("/", [](){ server.send(200, "text/html", getHtml()); });
    // AQUI PASAMOS LAS SEÑALES AL JSON
    server.on("/data", [](){ 
        String j="{\"p\":"+String(p_perc)+",\"s\":\""+p_stat+"\",\"l\":"+String(rx_rssi_lora)+",\"w\":"+String(WiFi.RSSI())+"}"; 
        server.send(200,"application/json",j); 
    });
    server.on("/cmd", handleCommand);
    server.on("/wifi", handleSaveWiFi);
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
    
    // Refresco pantalla si cambia WiFi
    static long lastWifiCheck = 0;
    if(millis() - lastWifiCheck > 2000) {
        lastWifiCheck = millis();
        if(WiFi.status() == WL_CONNECTED && WiFi.RSSI() != wifi_rssi) updateDisplay();
    }

    int btnState = digitalRead(PRG_BUTTON);
    if (btnState == LOW && !btn_pressed) { btn_pressed = true; btn_press_start = millis(); }
    if (btnState == HIGH && btn_pressed) {
        unsigned long duration = millis() - btn_press_start; btn_pressed = false;
        if (duration < 2000) sendCommand("ACT:PAUSE"); 
        else sendCommand("ACT:STOP");
    }
}
