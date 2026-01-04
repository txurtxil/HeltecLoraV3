/* CODIGO EMISOR V44 - CUSTOM WIFI AP + BARRA BOOT + FIXES */
#include "LoRaWan_APP.h"
#include <WiFi.h>
#include <WiFiClientSecure.h> 
#include <PubSubClient.h>
#include <Wire.h>
#include "HT_SSD1306Wire.h"
#include <SPI.h> 
#include <ArduinoJson.h> 
#include <WebServer.h>
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
WiFiClientSecure espClient; 
PubSubClient client(espClient);
WebServer server(80);
Preferences preferences; 
static RadioEvents_t RadioEvents;

// VARIABLES CONFIGURABLES
String printer_serial = "0309DA520500162"; 
String stored_access_code = "";
// NUEVAS VARIABLES PARA WIFI PROPIO
String ap_ssid = "HP_Emisor"; 
String ap_pass = ""; 

bool configMode = false;
String printer_ip = ""; bool printer_found = false; 
int lora_profile = 2; int lora_power = 14;
int print_percent=0; int time_remaining=0; String print_status="OFF";
int temp_nozzle=0; int temp_bed=0; int layer_num=0; int total_layer_num=0; int fan_speed=0;
const int BUFFER_SIZE = 20480; char jsonBuffer[BUFFER_SIZE]; 

// Variables pantalla
String last_cmd_screen = "";
long last_cmd_time = 0;

// --- FUNCIONES ---

void sendMqttCommand(String cmdRaw) {
    if(!client.connected()) return;
    
    // LIMPIEZA DE COMANDO (G-PAD FIX)
    cmdRaw.replace("+", " ");
    cmdRaw.replace("%20", " ");
    
    String jsonPayload = "";
    String topic_pub = "device/" + printer_serial + "/request";
    
    int sep = cmdRaw.indexOf(':');
    String type = cmdRaw.substring(0, sep);
    String val = cmdRaw.substring(sep+1);

    last_cmd_screen = "RX: " + val.substring(0, 10);
    last_cmd_time = millis();

    if(type == "ACT") {
        if(val == "PAUSE") jsonPayload = "{\"print\": {\"sequence_id\": \"0\", \"command\": \"pause\"}}";
        else if(val == "RESUME") jsonPayload = "{\"print\": {\"sequence_id\": \"0\", \"command\": \"resume\"}}";
        else if(val == "STOP") jsonPayload = "{\"print\": {\"sequence_id\": \"0\", \"command\": \"stop\"}}";
    }
    else if(type == "GCODE") jsonPayload = "{\"print\": {\"sequence_id\": \"0\", \"command\": \"gcode_line\", \"param\": \"" + val + "\\n\"}}";
    else if(type == "FILE") jsonPayload = "{\"print\": {\"command\": \"project_file\", \"url\": \"file:///sdcard/" + val + "\", \"param\": \"Metadata/plate_1.gcode\", \"subtask_id\": \"0\", \"use_ams\": false}}";
    
    if(jsonPayload != "") {
        client.publish(topic_pub.c_str(), jsonPayload.c_str());
    }
}

void OnRxDone(uint8_t *payload, uint16_t size, int16_t rssi, int8_t snr) {
    char msg[size+1]; memcpy(msg, payload, size); msg[size]=0; String cmd(msg);
    sendMqttCommand(cmd);
    Radio.Rx(0); 
}

void configLoRa() {
    int sf = 9; int bw = 0; 
    switch(lora_profile) { case 0: sf=7; bw=2; break; case 1: sf=7; bw=0; break; case 2: sf=9; bw=0; break; case 3: sf=12; bw=0; break; }
    Radio.SetTxConfig(MODEM_LORA, lora_power, 0, bw, sf, 1, 8, false, true, 0, 0, false, 3000);
    Radio.SetRxConfig(MODEM_LORA, bw, sf, 1, 0, 8, 0, false, 0, true, 0, 0, false, true);
    Radio.Rx(0); 
}

// --- WEB: AHORA INCLUYE CONFIGURACION WIFI ---
String getHtml() {
  String h = "<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1'><style>body{background:#111;color:#eee;font-family:sans-serif;text-align:center;}input,button{width:100%;padding:10px;margin:5px 0;box-sizing:border-box;} .box{border:1px solid #444; padding:10px; margin:10px; border-radius:10px;}</style></head><body>";
  
  if(configMode) h += "<h2 style='color:orange'>MODO CONFIG</h2>"; else h += "<h2>EMISOR V44</h2>";
  h += "<h3>" + String(print_percent) + "% " + print_status + "</h3>";
  
  // FORMULARIO IMPRESORA
  h += "<div class='box'><h3>🖨️ IMPRESORA</h3><form action='/save' method='POST'>";
  h += "<label>Serial Number:</label><input type='text' name='serial' value='" + printer_serial + "'>";
  h += "<label>Access Code:</label><input type='text' name='code' value='" + stored_access_code + "'>";
  
  // FORMULARIO WIFI PROPIO (NUEVO)
  h += "<h3>📶 ZONA WIFI (AP)</h3>";
  h += "<label>Nombre WiFi (SSID):</label><input type='text' name='ap_ssid' value='" + ap_ssid + "'>";
  h += "<label>Contraseña (Dejar vacía para abierta):</label><input type='text' name='ap_pass' value='" + ap_pass + "'>";
  
  h += "<button style='background:#0a0;color:white;font-weight:bold;margin-top:15px'>GUARDAR TODO</button></form></div>";
  
  // OTA
  h += "<div class='box'><h3>🔄 ACTUALIZAR</h3><form method='POST' action='/update' enctype='multipart/form-data'><input type='file' name='update' style='color:white'><button style='background:#d32f2f;color:white'>SUBIR .BIN</button></form></div></body></html>";
  return h;
}

void handleUpdate() {
  HTTPUpload& upload = server.upload();
  if (upload.status == UPLOAD_FILE_START) Update.begin(UPDATE_SIZE_UNKNOWN);
  else if (upload.status == UPLOAD_FILE_WRITE) Update.write(upload.buf, upload.currentSize);
  else if (upload.status == UPLOAD_FILE_END) { Update.end(true); server.send(200,"text/html","<h1>OK</h1>"); delay(500); ESP.restart(); }
}

void checkPrinter() {
    if(configMode) return;
    WiFiClientSecure p; p.setInsecure(); p.setTimeout(1); 
    String ips[] = {"192.168.4.2", "192.168.4.3", "192.168.4.4", "192.168.4.5"};
    for(String ip : ips) {
        if(p.connect(ip.c_str(), 8883)) {
            p.stop(); printer_ip=ip; printer_found=true; 
            client.setServer(printer_ip.c_str(), 8883); client.setCallback(callback); return;
        }
    }
}

// ================= SETUP =================
void setup() {
    Serial.begin(115200);
    pinMode(LED_PIN, OUTPUT); digitalWrite(LED_PIN, HIGH);
    pinMode(PRG_BUTTON, INPUT_PULLUP);
    delay(500); 

    // 1. FIX PANTALLA
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

    // 2. BARRA DE PROGRESO DE ARRANQUE
    for(int i=0; i<=100; i+=2) {
        screen.clear();
        screen.setTextAlignment(TEXT_ALIGN_CENTER);
        screen.setFont(ArialMT_Plain_10);
        screen.drawString(64, 10, "Pulsa PRG para CONFIG");
        screen.drawProgressBar(10, 30, 108, 10, i);
        screen.display();
        
        if(digitalRead(PRG_BUTTON) == LOW) {
            configMode = true;
            screen.clear();
            screen.setFont(ArialMT_Plain_16);
            screen.drawString(64, 20, "MODO CONFIG");
            screen.display();
            break; 
        }
        delay(30); 
    }
    
    if(!configMode) {
        screen.clear(); screen.drawString(64, 25, "Arrancando..."); screen.display();
    }

    // 3. CARGAR PREFERENCIAS (AHORA INCLUYE WIFI)
    preferences.begin("conf", false); 
    stored_access_code = preferences.getString("c", "");
    printer_serial = preferences.getString("ser", "0309DA520500162");
    // Cargamos nombre WiFi, si no existe usa HP_Emisor
    ap_ssid = preferences.getString("apssid", "HP_Emisor"); 
    // Cargamos password, si no existe es vacía
    ap_pass = preferences.getString("appass", ""); 
    preferences.end();
    
    // 4. INICIAR WIFI CON TUS DATOS PERSONALIZADOS
    WiFi.disconnect(true); 
    WiFi.mode(WIFI_AP); 
    if(ap_pass == "") {
        WiFi.softAP(ap_ssid.c_str(), NULL, 6, 0, 4); // Abierta
    } else {
        WiFi.softAP(ap_ssid.c_str(), ap_pass.c_str(), 6, 0, 4); // Con password
    }
    
    server.on("/", [](){ server.send(200, "text/html", getHtml()); });
    
    // GUARDADO DE DATOS (ACTUALIZADO)
    server.on("/save", [](){
        if(server.hasArg("code")){
            preferences.begin("conf", false); 
            preferences.putString("c", server.arg("code")); 
            preferences.putString("ser", server.arg("serial"));
            // Guardamos tambien WiFi
            if(server.hasArg("ap_ssid")) preferences.putString("apssid", server.arg("ap_ssid"));
            if(server.hasArg("ap_pass")) preferences.putString("appass", server.arg("ap_pass"));
            
            preferences.end(); 
            server.send(200, "text/html", "<h1>Guardado Correctamente. <br>Reiniciando...</h1>"); 
            delay(1000); ESP.restart();
        }
    });
    
    server.on("/update", HTTP_POST, [](){ server.send(200, "text/plain", (Update.hasError()) ? "FAIL" : "OK"); }, handleUpdate);
    server.begin();
    
    SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_CS);
    RadioEvents.TxDone = [](){ Radio.Rx(0); }; 
    RadioEvents.RxDone = OnRxDone;
    Radio.Init(&RadioEvents); Radio.SetChannel(RF_FREQUENCY);
    configLoRa();
    
    espClient.setInsecure(); espClient.setTimeout(5); client.setBufferSize(BUFFER_SIZE); client.setKeepAlive(45);
    digitalWrite(LED_PIN, LOW); 
}

void loop() {
    server.handleClient();
    if(configMode) return; 
    
    if(!client.connected()) {
        static long lastCheck=0;
        if(millis()-lastCheck > 5000) { 
            lastCheck=millis(); 
            if(WiFi.softAPgetStationNum()>0 && !printer_found) checkPrinter(); 
            if(printer_found && stored_access_code!="") reconnect(); 
        }
    } else client.loop();
    
    static long lastMsg=0;
    if(millis()-lastMsg > 2000) { 
        lastMsg=millis(); 
        sendLoRa(); 
        updateOled();
        digitalWrite(LED_PIN, HIGH); delay(50); digitalWrite(LED_PIN, LOW);
    }
    Radio.IrqProcess();
}

void reconnect() {
    String id = "E44-"+String(random(0xffff),HEX);
    if(client.connect(id.c_str(),"bblp",stored_access_code.c_str())) { 
        client.subscribe(("device/" + printer_serial + "/report").c_str()); 
    }
}

void callback(char* topic, byte* payload, unsigned int length) {
    if(length>=BUFFER_SIZE) return;
    memcpy(jsonBuffer, payload, length); jsonBuffer[length]=0;
    StaticJsonDocument<512> f; f["print"]["mc_percent"]=true; f["print"]["mc_remaining_time"]=true;
    f["print"]["gcode_state"]=true; f["print"]["nozzle_temper"]=true; f["print"]["bed_temper"]=true;
    StaticJsonDocument<2048> doc; deserializeJson(doc, jsonBuffer, DeserializationOption::Filter(f));
    JsonObject p = doc["print"];
    if(!p.isNull()) {
        if(p.containsKey("mc_percent")) print_percent=p["mc_percent"];
        if(p.containsKey("mc_remaining_time")) time_remaining=p["mc_remaining_time"];
        if(p.containsKey("gcode_state")) print_status=p["gcode_state"].as<String>();
        if(p.containsKey("nozzle_temper")) temp_nozzle=p["nozzle_temper"];
        if(p.containsKey("bed_temper")) temp_bed=p["bed_temper"];
    }
}

void sendLoRa() {
    char p[100]; snprintf(p,100,"%d|%d|%s|%d|%d",print_percent,time_remaining,print_status.c_str(),temp_nozzle,temp_bed);
    Radio.Send((uint8_t*)p, strlen(p));
}

// PANTALLA OLED ORDENADA
void updateOled() {
    screen.clear(); 
    screen.setFont(ArialMT_Plain_10); 
    
    // LINEA 1
    screen.setTextAlignment(TEXT_ALIGN_LEFT); 
    if(client.connected()) screen.drawString(0,0, "LINK: OK");
    else if(printer_found) screen.drawString(0,0, "LINK: Conn");
    else screen.drawString(0,0, "LINK: Buscar");

    screen.setTextAlignment(TEXT_ALIGN_RIGHT);
    // Muestra clientes conectados al AP (la impresora)
    screen.drawString(128, 0, "Cli: " + String(WiFi.softAPgetStationNum()));

    if(millis() - last_cmd_time < 3000 && last_cmd_screen != "") {
        screen.setTextAlignment(TEXT_ALIGN_CENTER);
        screen.setFont(ArialMT_Plain_16);
        screen.drawString(64, 25, last_cmd_screen);
    } 
    else {
        screen.setTextAlignment(TEXT_ALIGN_LEFT); 
        screen.setFont(ArialMT_Plain_24);
        screen.drawString(0, 16, String(print_percent) + "%");
        screen.setFont(ArialMT_Plain_10);
        screen.drawString(60, 16, print_status.substring(0, 8)); 
        screen.drawString(60, 28, String(time_remaining) + " min");
        screen.drawLine(0, 46, 128, 46);
        String temps = "N:" + String(temp_nozzle) + " B:" + String(temp_bed);
        screen.drawString(0, 49, temps);
    }
    
    screen.display();
}
