/* CODIGO RECEPTOR V53 - NOMBRE PIEZA + TITLE SPEED UPDATE */
#include "LoRaWan_APP.h"
#include <WiFi.h>
#include <WebServer.h>
#include <Wire.h>
#include "HT_SSD1306Wire.h"
#include <Preferences.h>
#include <Update.h>

#define PRG_BUTTON 0

// PINES HELTEC V3
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

int rx_rssi_lora=0; long wifi_rssi=0; unsigned long last_packet=0; bool signal_lost=false;

// ESTADO AMPLIADO V53
int p_perc=0; int p_time=0; String p_stat="ESPERANDO"; 
int p_noz=0; int p_bed=0;
int p_lay=0; int p_totlay=0; int p_fan=0;
int p_spd=2; String p_file="--";

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
    screen.clear(); screen.setFont(ArialMT_Plain_10); 
    
    screen.setTextAlignment(TEXT_ALIGN_LEFT); 
    if(WiFi.status() == WL_CONNECTED) { screen.drawString(0, 0, WiFi.localIP().toString()); wifi_rssi = WiFi.RSSI(); } 
    else { screen.drawString(0, 0, "AP: 192.168.4.1"); wifi_rssi = 0; }

    screen.setTextAlignment(TEXT_ALIGN_RIGHT); 
    String sig = "L:" + String(rx_rssi_lora);
    if(wifi_rssi != 0) sig += " W:" + String(wifi_rssi); else sig += " W:--";
    screen.drawString(128, 0, sig);

    if(signal_lost) {
        if((millis()/500)%2==0) {
            screen.setTextAlignment(TEXT_ALIGN_CENTER); screen.setFont(ArialMT_Plain_16);
            screen.drawString(64, 25, "¡SIN SEÑAL!");
        }
    } else {
        screen.setTextAlignment(TEXT_ALIGN_LEFT); 
        screen.setFont(ArialMT_Plain_24);
        screen.drawString(0, 15, String(p_perc) + "%");
        
        screen.setFont(ArialMT_Plain_10);
        // AQUI MOSTRAMOS EL FICHERO EN VEZ DE ESTADO SI ESTA IMPRIMIENDO
        if(p_stat == "RUNNING") screen.drawString(60, 15, p_file);
        else screen.drawString(60, 15, p_stat.substring(0, 10)); 
        
        screen.drawString(60, 26, "Lay: " + String(p_lay) + "/" + String(p_totlay));
        screen.drawString(60, 36, "Fan: " + String(p_fan) + "%");
        screen.drawLine(0, 48, 128, 48);
        String temps = "N:" + String(p_noz) + " B:" + String(p_bed) + " T:" + String(p_time)+"m";
        screen.drawString(0, 50, temps);
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

void handleCommand() {
    if(server.hasArg("act")) { sendCommand("ACT:"+server.arg("act")); server.send(200, "text/plain", "OK"); } 
    else if(server.hasArg("gcode")) { sendCommand("GCODE:"+server.arg("gcode")); server.send(200, "text/plain", "OK"); }
    else if(server.hasArg("file")) { sendCommand("FILE:"+server.arg("file")); server.send(200, "text/plain", "OK"); }
    else server.send(400, "text/plain", "Error");
}

void handleSaveWiFi() {
    if(server.hasArg("ssid") && server.hasArg("pass")) {
        preferences.begin("conf", false); preferences.putString("ssid", server.arg("ssid")); preferences.putString("pass", server.arg("pass")); preferences.end();
        server.send(200, "text/html", "<h1>WiFi Guardado.</h1>"); delay(1000); ESP.restart();
    } else server.send(400, "text/plain", "Error");
}

void handleSaveLoRa() {
    if(server.hasArg("prof") && server.hasArg("pow")) {
        preferences.begin("conf", false); preferences.putInt("prof", server.arg("prof").toInt()); preferences.putInt("pow", server.arg("pow").toInt()); preferences.end();
        server.send(200, "text/html", "<h1>LoRa Guardado.</h1>"); delay(1000); ESP.restart();
    } else server.send(400, "text/plain", "Error");
}

void handleUpdate() {
  HTTPUpload& upload = server.upload();
  if (upload.status == UPLOAD_FILE_START) Update.begin(UPDATE_SIZE_UNKNOWN);
  else if (upload.status == UPLOAD_FILE_WRITE) Update.write(upload.buf, upload.currentSize);
  else if (upload.status == UPLOAD_FILE_END) { Update.end(true); server.send(200,"text/html","<h1>OK</h1>"); delay(1000); ESP.restart(); }
}

String getHtml() {
  String h = "<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1'>";
  h += "<style>";
  h += "body{background:#111;color:#eee;font-family:sans-serif;text-align:center;margin:0;padding:5px;}";
  h += ".card{background:#222;padding:10px;margin:10px auto;border-radius:12px;max-width:400px;border:1px solid #444;}";
  h += "h1{margin:0;font-size:35px;color:#00d2ff;} h3{border-bottom:1px solid #555;padding-bottom:5px;color:#aaa;}";
  h += ".sig-bar{font-size:12px;background:#000;padding:5px;border-radius:5px;margin-bottom:10px;color:#0f0;}";
  h += "input,select{width:65%;padding:8px;background:#333;border:1px solid #555;color:white;border-radius:5px;}";
  h += "button{padding:8px 15px;border:none;border-radius:5px;color:white;font-weight:bold;cursor:pointer;margin:2px;}";
  h += ".btn-blue{background:#007bff;} .btn-green{background:#28a745;} .btn-red{background:#dc3545;} .btn-yell{background:#ffc107;color:black;} .btn-gray{background:#555;}";
  h += ".stat-grid{display:grid;grid-template-columns:1fr 1fr;gap:5px;text-align:left;padding:10px;}";
  h += ".stat-box{background:#333;padding:5px;border-radius:5px;}";
  h += ".grid-2{display:grid;grid-template-columns:1fr 1fr;gap:5px;}";
  h += "</style>";
  
  h += "<script>";
  h += "setInterval(()=>{fetch('/data').then(r=>r.json()).then(d=>{";
  h += "document.getElementById('p').innerText=d.p+'%';document.getElementById('s').innerText=d.s;";
  h += "document.getElementById('lay').innerText=d.lay+' / '+d.totlay;";
  h += "document.getElementById('fan').innerText=d.fan+'%';";
  h += "document.getElementById('noz').innerText=d.noz+'°C';";
  h += "document.getElementById('bed').innerText=d.bed+'°C';";
  h += "document.getElementById('tim').innerText=d.tim+' min';";
  // Nombre fichero en Header
  h += "document.getElementById('fn').innerText=d.fn;"; 
  // Titulo Velocidad Dinamico
  h += "let spdNames=['?','Silencioso','Normal','Sport','Ludicrous'];";
  h += "document.getElementById('spd_title').innerText='🚀 VELOCIDAD: '+spdNames[d.spd];";
  
  h += "document.getElementById('sig').innerText='LoRa: '+d.l+'dBm | WiFi: '+d.w+'dBm';})},2000);";
  h += "function c(u){ fetch(u.replace(/ /g, '+')); }"; 
  h += "</script></head><body><h2>🛸 RECEPTOR V53</h2>";
  
  h += "<div class='sig-bar' id='sig'>Cargando...</div>";
  // HEADER CON NOMBRE DE FICHERO
  h += "<div class='card'><h3 id='fn' style='color:#0ff;margin:0;'>--</h3><h1 id='p' style='font-size:50px'>"+String(p_perc)+"%</h1><p id='s'>"+p_stat+"</p></div>";
  
  h += "<div class='card'><h3>📊 DATOS IMPRESORA</h3><div class='stat-grid'>";
  h += "<div class='stat-box'>🌡️ Nozzle: <span id='noz'>"+String(p_noz)+"°C</span></div>";
  h += "<div class='stat-box'>🛌 Bed: <span id='bed'>"+String(p_bed)+"°C</span></div>";
  h += "<div class='stat-box'>🍰 Capa: <span id='lay'>"+String(p_lay)+" / "+String(p_totlay)+"</span></div>";
  h += "<div class='stat-box'>🌀 Fan: <span id='fan'>"+String(p_fan)+"%</span></div>";
  h += "<div class='stat-box'>⏳ Restante: <span id='tim'>"+String(p_time)+" min</span></div>";
  h += "</div></div>";

  // TITULO DE VELOCIDAD DINAMICO (ID=spd_title)
  h += "<div class='card'><h3 id='spd_title'>🚀 VELOCIDAD</h3><div class='grid-2'>";
  h += "<button class='btn-green' onclick=\"c('/cmd?gcode=M220 S50')\">Silencioso (50%)</button>";
  h += "<button class='btn-blue' onclick=\"c('/cmd?gcode=M220 S100')\">Normal (100%)</button>";
  h += "<button class='btn-yell' onclick=\"c('/cmd?gcode=M220 S124')\">Sport (124%)</button>";
  h += "<button class='btn-red' onclick=\"c('/cmd?gcode=M220 S166')\">Ludicrous (166%)</button>";
  h += "</div></div>";

  h += "<div class='card'><h3>⏯ CONTROL</h3>";
  h += "<button class='btn-yell' style='width:100%;margin-bottom:10px' onclick=\"c('/cmd?gcode=G28')\">🏠 HOME (G28)</button>";
  h += "<button class='btn-yell' onclick=\"c('/cmd?act=PAUSE')\">PAUSA</button><button class='btn-green' onclick=\"c('/cmd?act=RESUME')\">PLAY</button><button class='btn-red' onclick=\"c('/cmd?act=STOP')\">STOP</button></div>";

  h += "<div class='card'><h3>📂 ARCHIVOS / CMD</h3>";
  h += "<input type='text' id='f' placeholder='archivo.gcode'><button class='btn-blue' onclick=\"c('/cmd?file='+document.getElementById('f').value)\">Print</button><br>";
  h += "<input type='text' id='g' placeholder='Gcode'><button class='btn-blue' onclick=\"c('/cmd?gcode='+document.getElementById('g').value)\">Send</button></div>";

  h += "<div class='card'><h3>⚙️ SISTEMA</h3>";
  h += "<form action='/wifi' method='POST'><input type='text' name='ssid' placeholder='SSID' value='"+wifi_sta_ssid+"'><input type='password' name='pass' placeholder='Pass'><button class='btn-green'>WiFi</button></form><br>";
  h += "<form action='/lora' method='POST'><select name='prof'><option value='0'>Fast</option><option value='2' selected>Bal</option><option value='3'>Far</option></select><button class='btn-green'>LoRa</button></form><br>";
  h += "<form method='POST' action='/update' enctype='multipart/form-data'><input type='file' name='update' style='width:60%'><button class='btn-yell'>OTA</button></form></div>";

  h += "</body></html>";
  return h;
}

// ================= SETUP =================
void setup() {
    Serial.begin(115200); pinMode(LED_PIN, OUTPUT); digitalWrite(LED_PIN, HIGH);
    pinMode(PRG_BUTTON, INPUT_PULLUP); delay(1000);

    // FIX OLED
    pinMode(Vext, OUTPUT); pinMode(RST_OLED, OUTPUT);
    digitalWrite(Vext, HIGH); delay(300); digitalWrite(Vext, LOW); delay(500);
    digitalWrite(Vext, HIGH); delay(300); digitalWrite(Vext, LOW); delay(500);
    digitalWrite(RST_OLED, LOW); delay(300); digitalWrite(RST_OLED, HIGH); delay(300);
    digitalWrite(RST_OLED, LOW); delay(300); digitalWrite(RST_OLED, HIGH); delay(300);
    
    screen.init();
    if (screen.getStringWidth("T") == 0) { 
        digitalWrite(Vext, HIGH); delay(200); digitalWrite(Vext, LOW); delay(500);
        digitalWrite(RST_OLED, LOW); delay(500); digitalWrite(RST_OLED, HIGH); delay(500);
        screen.init();
    }
    screen.flipScreenVertically(); screen.setFont(ArialMT_Plain_10);
    screen.clear(); screen.drawString(0,0,"INICIANDO V53..."); screen.display();
    
    preferences.begin("conf", false);
    lora_profile = preferences.getInt("prof", 2); lora_power = preferences.getInt("pow", 14);
    wifi_sta_ssid = preferences.getString("ssid", ""); wifi_sta_pass = preferences.getString("pass", ""); wifi_ap_pass = preferences.getString("appass", "");
    preferences.end();
    
    WiFi.mode(WIFI_AP_STA);
    if(wifi_ap_pass == "") WiFi.softAP("HP_Receptor", NULL); else WiFi.softAP("HP_Receptor", wifi_ap_pass.c_str());
    if(wifi_sta_ssid != "") WiFi.begin(wifi_sta_ssid.c_str(), wifi_sta_pass.c_str());

    server.on("/", [](){ server.send(200, "text/html", getHtml()); });
    // JSON AMPLIADO V53
    server.on("/data", [](){ 
        String j="{\"p\":"+String(p_perc)+",\"s\":\""+p_stat+"\",\"l\":"+String(rx_rssi_lora)+",\"w\":"+String(WiFi.RSSI());
        j += ",\"noz\":"+String(p_noz)+",\"bed\":"+String(p_bed)+",\"tim\":"+String(p_time);
        j += ",\"lay\":"+String(p_lay)+",\"totlay\":"+String(p_totlay)+",\"fan\":"+String(p_fan);
        j += ",\"spd\":"+String(p_spd)+",\"fn\":\""+p_file+"\"}";
        server.send(200,"application/json",j); 
    });
    server.on("/cmd", handleCommand); server.on("/wifi", handleSaveWiFi); server.on("/lora", handleSaveLoRa);
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
            p_lay=getValue(d,'|',5).toInt(); p_totlay=getValue(d,'|',6).toInt(); p_fan=getValue(d,'|',7).toInt();
            // NUEVOS DATOS V53
            p_spd=getValue(d,'|',8).toInt(); p_file=getValue(d,'|',9);
            
            updateDisplay(); digitalWrite(LED_PIN, HIGH); delay(50); digitalWrite(LED_PIN, LOW);
        }
        Radio.Rx(0);
    };
    Radio.Init(&RadioEvents); Radio.SetChannel(RF_FREQUENCY);
    configLoRa();
    updateDisplay(); digitalWrite(LED_PIN, LOW);
}

void loop() {
    server.handleClient(); Radio.IrqProcess();
    if(millis()-last_packet > 30000 && !signal_lost) { signal_lost=true; updateDisplay(); }
    static long lastWifiCheck = 0;
    if(millis() - lastWifiCheck > 2000) {
        lastWifiCheck = millis();
        if(WiFi.status() == WL_CONNECTED && WiFi.RSSI() != wifi_rssi) updateDisplay();
    }
    int btnState = digitalRead(PRG_BUTTON);
    if (btnState == LOW && !btn_pressed) { btn_pressed = true; btn_press_start = millis(); }
    if (btnState == HIGH && btn_pressed) {
        unsigned long duration = millis() - btn_press_start; btn_pressed = false;
        if (duration < 2000) sendCommand("ACT:PAUSE"); else sendCommand("ACT:STOP");
    }
}
