/* CODIGO RECEPTOR V37 - FULL CNC CONTROL & WIFI FIX */
/* Novedades: Joystick XY, Corrección Z, Home Central, Dashboard Completo */

#include "LoRaWan_APP.h"
#include <WiFi.h>
#include <WebServer.h>
#include <Wire.h>
#include "HT_SSD1306Wire.h"
#include <Preferences.h>
#include <Update.h>

#define PRG_BUTTON 0

// HARDWARE
#define SDA_OLED 17
#define SCL_OLED 18
#define RST_OLED 21
#define Vext 25
#define LORA_SCK 9
#define LORA_MISO 11
#define LORA_MOSI 10
#define LORA_CS 8
#define RF_FREQUENCY 868100000

SSD1306Wire screen(0x3c, 500000, SDA_OLED, SCL_OLED, GEOMETRY_128_64, RST_OLED);
WebServer server(80);
Preferences preferences;
static RadioEvents_t RadioEvents;

// DATOS
int rx_rssi_lora=0; 
long wifi_rssi=0;
unsigned long last_packet=0; 
bool signal_lost=false;

// Variables Impresora
int p_perc=0; int p_time=0; String p_stat="ESPERANDO"; 
int p_noz=0; int p_bed=0; int p_lay=0; int p_tot=0; int p_fan=0;

// CONFIG
int lora_profile=2; int lora_power=14;
String wifi_sta_ssid=""; String wifi_sta_pass=""; String wifi_ap_pass=""; 

// LOGICA BOTON
unsigned long btn_press_start = 0;
bool btn_pressed = false;

// HELPER PARSEO
String getValue(String d, char s, int i) {
    int f=0; int str[]={0,-1}; int m=d.length()-1;
    for(int j=0; j<=m && f<=i; j++) { if(d.charAt(j)==s || j==m) { f++; str[0]=str[1]+1; str[1]=(j==m)?j+1:j; } }
    return f>i ? d.substring(str[0], str[1]) : "";
}

// ================== PANTALLA OLED ==================
void updateDisplay() {
    screen.clear(); 
    screen.setFont(ArialMT_Plain_10); 
    
    // Header
    String ipStr = (WiFi.status() == WL_CONNECTED) ? WiFi.localIP().toString() : "AP: 192.168.4.1";
    screen.setTextAlignment(TEXT_ALIGN_LEFT); screen.drawString(0, 0, ipStr);
    screen.setTextAlignment(TEXT_ALIGN_RIGHT); screen.drawString(128, 0, "LoRa " + String(rx_rssi_lora));

    if(signal_lost) {
        screen.setTextAlignment(TEXT_ALIGN_CENTER); screen.setFont(ArialMT_Plain_16);
        screen.drawString(64, 25, "SIN SEÑAL");
        screen.setFont(ArialMT_Plain_10); screen.drawString(64, 45, "Esperando Emisor...");
    } else {
        screen.setTextAlignment(TEXT_ALIGN_LEFT); screen.setFont(ArialMT_Plain_24);
        screen.drawString(0, 16, String(p_perc) + "%");
        
        screen.setFont(ArialMT_Plain_10);
        String shortStat = p_stat; if(shortStat.length()>9) shortStat = shortStat.substring(0, 9);
        screen.drawString(58, 16, shortStat); 
        screen.drawString(58, 28, String(p_time) + " min rest.");

        screen.drawLine(0, 46, 128, 46);
        String footer = "N:" + String(p_noz) + " B:" + String(p_bed) + " L:" + String(p_lay) + "/" + String(p_tot);
        screen.drawString(0, 49, footer);
    }
    screen.display();
}

// ================== LORA & COMANDOS ==================
void configLoRa() {
    int sf = 9; int bw = 0; 
    switch(lora_profile) {
        case 0: sf=7; bw=2; break; 
        case 1: sf=7; bw=0; break; 
        case 2: sf=9; bw=0; break; 
        case 3: sf=12; bw=0; break; 
    }
    Radio.SetTxConfig(MODEM_LORA, lora_power, 0, bw, sf, 1, 8, false, true, 0, 0, false, 3000);
    Radio.SetRxConfig(MODEM_LORA, bw, sf, 1, 0, 8, 0, false, 0, true, 0, 0, false, true);
    Radio.Rx(0);
}

void sendCommand(String cmd) {
    String packet = "CMD:" + cmd;
    screen.clear(); screen.setFont(ArialMT_Plain_16); screen.setTextAlignment(TEXT_ALIGN_CENTER);
    screen.drawString(64, 20, "ENVIANDO..."); screen.drawString(64, 40, cmd); screen.display();
    Radio.Send((uint8_t*)packet.c_str(), packet.length());
}

void handleCommand() {
    if(server.hasArg("act")) {
        String act = server.arg("act"); sendCommand(act);
        server.send(200, "text/plain", "OK: " + act);
    } else server.send(400, "text/plain", "Falta argumento");
}

// ================== WEB SERVER (DASHBOARD & JOYSTICK) ==================
String getHtml() {
  String h = "<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1'>";
  
  // CSS ESTILIZADO
  h += "<style>";
  h += "body{background:#111;color:#eee;font-family:sans-serif;text-align:center;margin:0;padding:5px;}";
  h += ".card{background:#222;padding:10px;margin:10px auto;border-radius:12px;max-width:400px;border:1px solid #333;}";
  h += ".grid{display:grid;grid-template-columns:1fr 1fr;gap:8px;}";
  h += ".metric{background:#333;padding:8px;border-radius:6px;}";
  h += ".val{font-size:16px;font-weight:bold;color:#fff;}";
  h += ".label{font-size:11px;color:#aaa;}";
  
  // ESTILOS JOYSTICK
  h += ".dpad{display:grid;grid-template-columns:1fr 1fr 1fr;gap:5px;max-width:220px;margin:0 auto;}";
  h += "button{width:100%;padding:12px;margin:3px 0;border:none;border-radius:6px;font-weight:bold;cursor:pointer;color:white;font-size:14px;}";
  h += ".btn-move{background:#444;font-size:18px;} .btn-home{background:#1976d2;} .btn-act{background:#e3b341;color:#000;} .btn-stop{background:#d32f2f;} .btn-z{background:#555;}";
  h += "input{width:90%;padding:8px;background:#111;border:1px solid #444;color:white;border-radius:4px;}";
  h += "</style>";
  
  // JS
  h += "<script>setInterval(()=>{fetch('/data').then(r=>r.json()).then(d=>{";
  h += "document.getElementById('p').innerText=d.p+'%'; document.getElementById('s').innerText=d.s;";
  h += "document.getElementById('t_n').innerText=d.n+'°C'; document.getElementById('t_b').innerText=d.b+'°C';";
  h += "document.getElementById('fan').innerText=d.f+'%'; document.getElementById('rssi_l').innerText=d.rl+' dBm';";
  h += "document.getElementById('rssi_w').innerText=d.rw+' dBm'; document.getElementById('lay').innerText=d.l+' / '+d.tl;";
  h += "})},2000); function cmd(c){fetch('/cmd?act='+c);}</script></head>";
  
  h += "<body><h2>🛸 RECEPTOR V37</h2>";

  // 1. DASHBOARD COMPACTO
  h += "<div class='card'>";
  h += "<div id='p' style='font-size:50px;color:#4caf50;line-height:1'>"+String(p_perc)+"%</div>";
  h += "<div id='s' style='font-size:16px;color:#aaa'>"+p_stat+"</div>";
  h += "<hr style='border:0;border-top:1px solid #444;margin:10px 0'>";
  h += "<div class='grid'>";
  h += "<div class='metric'><div class='label'>NOZZLE</div><div id='t_n' class='val' style='color:#ff5252'>"+String(p_noz)+"°C</div></div>";
  h += "<div class='metric'><div class='label'>CAMA</div><div id='t_b' class='val' style='color:#ff5252'>"+String(p_bed)+"°C</div></div>";
  h += "<div class='metric'><div class='label'>FAN</div><div id='fan' class='val' style='color:#40c4ff'>"+String(p_fan)+"%</div></div>";
  h += "<div class='metric'><div class='label'>CAPAS</div><div id='lay' class='val'>"+String(p_lay)+"/"+String(p_tot)+"</div></div>";
  h += "<div class='metric'><div class='label'>LORA</div><div id='rssi_l' class='val' style='color:#ffd740'>"+String(rx_rssi_lora)+"</div></div>";
  h += "<div class='metric'><div class='label'>WIFI</div><div id='rssi_w' class='val' style='color:#ffd740'>"+String(wifi_rssi)+"</div></div>";
  h += "</div></div>";

  // 2. JOYSTICK X/Y/Z + HOME
  h += "<div class='card'><h3>🕹️ MOVIMIENTO</h3>";
  
  // XY PAD
  h += "<div class='dpad' style='margin-bottom:15px;'>";
  h += "<div></div> <button class='btn-move' onclick=\"cmd('MOVE:Y:10')\">▲ Y+</button> <div></div>"; // Y+ Fondo
  h += "<button class='btn-move' onclick=\"cmd('MOVE:X:-10')\">◀ X-</button>";
  h += "<button class='btn-home' onclick=\"cmd('HOME')\">🏠</button>"; // HOME en el centro
  h += "<button class='btn-move' onclick=\"cmd('MOVE:X:10')\">X+ ▶</button>";
  h += "<div></div> <button class='btn-move' onclick=\"cmd('MOVE:Y:-10')\">▼ Y-</button> <div></div>"; // Y- Frente
  h += "</div>";
  
  // Z CONTROL (Corregido)
  h += "<div class='grid'>";
  h += "<button class='btn-z' onclick=\"cmd('MOVE:Z:10')\">⬆ SUBIR (Z+)</button>"; // Z+ Sube
  h += "<button class='btn-z' onclick=\"cmd('MOVE:Z:-10')\">⬇ BAJAR (Z-)</button>"; // Z- Baja
  h += "</div></div>";

  // 3. CONTROLES GENERALES
  h += "<div class='card'><h3>🎮 ACCIONES</h3>";
  h += "<div class='grid'>";
  h += "<button class='btn-act' onclick=\"cmd('PAUSE')\">⏸ PAUSA</button><button class='btn-act' onclick=\"cmd('RESUME')\">▶ PLAY</button>";
  h += "</div><button class='btn-stop' onclick=\"cmd('STOP')\">🛑 STOP EMERGENCIA</button>";
  h += "<div class='grid' style='margin-top:5px;'><button onclick=\"cmd('LIGHT_ON')\" style='background:#555'>LUZ ON</button><button onclick=\"cmd('LIGHT_OFF')\" style='background:#555'>LUZ OFF</button></div>";
  h += "</div>";

  // 4. CONFIG & OTA
  h += "<div class='card'><h3>⚙️ SISTEMA</h3><form action='/save' method='POST'>";
  h += "<input type='text' name='ssid' value='"+wifi_sta_ssid+"' placeholder='SSID Casa'>";
  h += "<input type='text' name='pass' value='"+wifi_sta_pass+"' placeholder='Pass Casa' style='margin-top:5px;'>";
  h += "<button type='submit' style='background:#333;margin-top:5px;'>GUARDAR WIFI</button></form>";
  h += "<form method='POST' action='/update' enctype='multipart/form-data' style='margin-top:10px;'>";
  h += "<input type='file' name='update' style='border:none'><button style='background:#555'>ACTUALIZAR FIRMWARE</button></form></div>";
  
  h += "</body></html>";
  return h;
}

void handleUpdate() {
  HTTPUpload& upload = server.upload();
  if (upload.status == UPLOAD_FILE_START) Update.begin(UPDATE_SIZE_UNKNOWN);
  else if (upload.status == UPLOAD_FILE_WRITE) Update.write(upload.buf, upload.currentSize);
  else if (upload.status == UPLOAD_FILE_END) { Update.end(true); server.send(200,"text/html","<h1>OK!</h1>"); delay(1000); ESP.restart(); }
}

// ================== SETUP ==================
void setup() {
    Serial.begin(115200);
    pinMode(PRG_BUTTON, INPUT_PULLUP);

    preferences.begin("conf", false);
    lora_profile = preferences.getInt("prof", 2); lora_power = preferences.getInt("pow", 14);
    wifi_sta_ssid = preferences.getString("ssid", ""); wifi_sta_pass = preferences.getString("pass", ""); wifi_ap_pass = preferences.getString("appass", "");
    preferences.end();

    pinMode(Vext,OUTPUT); digitalWrite(Vext,HIGH); delay(100); digitalWrite(Vext,LOW); delay(100);               
    pinMode(RST_OLED,OUTPUT); digitalWrite(RST_OLED,LOW); delay(200); digitalWrite(RST_OLED,HIGH); delay(200);   
    screen.init(); screen.setFont(ArialMT_Plain_10); screen.flipScreenVertically();
    
    // WIFI FIX: AP + STA (No Blocking)
    WiFi.mode(WIFI_AP_STA);
    if(wifi_ap_pass == "") WiFi.softAP("HP_Receptor", NULL); else WiFi.softAP("HP_Receptor", wifi_ap_pass.c_str());
    if(wifi_sta_ssid != "") WiFi.begin(wifi_sta_ssid.c_str(), wifi_sta_pass.c_str());

    server.on("/", [](){ server.send(200, "text/html", getHtml()); });
    server.on("/data", [](){ 
        String j="{\"p\":"+String(p_perc)+",\"s\":\""+p_stat+"\",\"n\":"+String(p_noz)+",\"b\":"+String(p_bed);
        j += ",\"f\":"+String(p_fan)+",\"l\":"+String(p_lay)+",\"tl\":"+String(p_tot);
        j += ",\"rl\":"+String(rx_rssi_lora)+",\"rw\":"+String(wifi_rssi)+"}";
        server.send(200,"application/json",j); 
    });
    server.on("/cmd", handleCommand);
    server.on("/save", [](){
        if(server.hasArg("ssid")){
            preferences.begin("conf", false); preferences.putString("ssid", server.arg("ssid")); preferences.putString("pass", server.arg("pass")); preferences.end(); 
            server.send(200, "text/html", "Guardado. Reiniciando..."); delay(500); ESP.restart();
        }
    });
    server.on("/update", HTTP_POST, [](){ server.send(200, "text/plain", (Update.hasError())?"FAIL":"OK"); }, handleUpdate);
    server.begin();

    SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_CS);
    RadioEvents.TxDone = [](){ Radio.Rx(0); }; 
    RadioEvents.RxDone = [](uint8_t *payload, uint16_t size, int16_t rssi, int8_t snr){
        rx_rssi_lora = rssi; last_packet = millis(); signal_lost=false;
        char msg[size+1]; memcpy(msg, payload, size); msg[size]=0; String d(msg);
        if(d.indexOf('|')>0) {
            p_perc=getValue(d,'|',0).toInt(); p_time=getValue(d,'|',1).toInt(); p_stat=getValue(d,'|',2);
            p_noz=getValue(d,'|',3).toInt(); p_bed=getValue(d,'|',4).toInt(); p_lay=getValue(d,'|',5).toInt();
            p_tot=getValue(d,'|',6).toInt(); p_fan=getValue(d,'|',7).toInt();
            updateDisplay();
        }
        Radio.Rx(0);
    };
    Radio.Init(&RadioEvents); Radio.SetChannel(RF_FREQUENCY);
    configLoRa();
    updateDisplay();
}

void loop() {
    server.handleClient();
    Radio.IrqProcess();

    static long last_w_check = 0;
    if(millis() - last_w_check > 2000) { last_w_check = millis(); wifi_rssi = WiFi.RSSI(); }
    if(millis()-last_packet > 10000 && !signal_lost) { signal_lost=true; rx_rssi_lora=-150; updateDisplay(); }

    int btnState = digitalRead(PRG_BUTTON);
    if (btnState == LOW && !btn_pressed) { btn_pressed = true; btn_press_start = millis(); }
    if (btnState == HIGH && btn_pressed) {
        unsigned long duration = millis() - btn_press_start; btn_pressed = false;
        if (duration < 2000) { if(p_stat == "RUNNING") sendCommand("PAUSE"); else sendCommand("RESUME"); } 
        else sendCommand("STOP");
    }
}