/* CODIGO EMISOR V41 - ULTIMATE COLD BOOT FIX (PIN 36) */
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

// --- CONFIGURACIÓN PINES HELTEC V3 (CORRECTOS) ---
#define SDA_OLED 17
#define SCL_OLED 18  
#define RST_OLED 21
#define Vext 36      // <--- LA CLAVE: Pin 36 alimenta el OLED en V3
#define LED_PIN 35   

#define LORA_SCK 9
#define LORA_MISO 11
#define LORA_MOSI 10
#define LORA_CS 8
#define RF_FREQUENCY 868100000

// Configuración Red
const char* ssid_ap_default = "HP_Emisor_V41";     
String printer_serial = "0309DA520500162"; 

SSD1306Wire screen(0x3c, 500000, SDA_OLED, SCL_OLED, GEOMETRY_128_64, RST_OLED);
WiFiClientSecure espClient; 
PubSubClient client(espClient);
WebServer server(80);
Preferences preferences; 
static RadioEvents_t RadioEvents;

// Variables Globales
bool configMode = false;
String printer_ip = ""; bool printer_found = false; String stored_access_code = "";
int lora_profile = 2; int lora_power = 14;
int print_percent=0; int time_remaining=0; String print_status="OFF";
int temp_nozzle=0; int temp_bed=0; int layer_num=0; int total_layer_num=0; int fan_speed=0;
const int BUFFER_SIZE = 20480; char jsonBuffer[BUFFER_SIZE]; 

// --- FUNCIONES ---

void sendMqttCommand(String cmdRaw) {
    if(!client.connected()) return;
    String jsonPayload = "";
    String topic_pub = "device/" + printer_serial + "/request";
    int sep = cmdRaw.indexOf(':');
    String type = cmdRaw.substring(0, sep);
    String val = cmdRaw.substring(sep+1);

    if(type == "ACT") {
        if(val == "PAUSE") jsonPayload = "{\"print\": {\"sequence_id\": \"0\", \"command\": \"pause\"}}";
        else if(val == "RESUME") jsonPayload = "{\"print\": {\"sequence_id\": \"0\", \"command\": \"resume\"}}";
        else if(val == "STOP") jsonPayload = "{\"print\": {\"sequence_id\": \"0\", \"command\": \"stop\"}}";
    }
    else if(type == "GCODE") jsonPayload = "{\"print\": {\"sequence_id\": \"0\", \"command\": \"gcode_line\", \"param\": \"" + val + "\\n\"}}";
    else if(type == "FILE") jsonPayload = "{\"print\": {\"command\": \"project_file\", \"url\": \"file:///sdcard/" + val + "\", \"param\": \"Metadata/plate_1.gcode\", \"subtask_id\": \"0\", \"use_ams\": false}}";
    
    if(jsonPayload != "") {
        client.publish(topic_pub.c_str(), jsonPayload.c_str());
        screen.clear(); screen.setFont(ArialMT_Plain_16); 
        screen.setTextAlignment(TEXT_ALIGN_CENTER);
        screen.drawString(64,20, "CMD ENVIADO"); screen.display();
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

String getHtml() {
  String h = "<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1'><style>body{background:#111;color:#eee;font-family:sans-serif;text-align:center;}input,button{width:100%;padding:10px;margin:5px 0;}</style></head><body>";
  if(configMode) h += "<h2 style='color:orange'>MODO CONFIG</h2>"; else h += "<h2>EMISOR V41</h2>";
  h += "<h3>" + String(print_percent) + "% " + print_status + "</h3>";
  h += "<form action='/save' method='POST'><label>Serial:</label><input type='text' name='serial' value='" + printer_serial + "'><label>Code:</label><input type='text' name='code' value='" + stored_access_code + "'><button>GUARDAR</button></form>";
  h += "<form method='POST' action='/update' enctype='multipart/form-data'><input type='file' name='update'><button>SUBIR BIN</button></form></body></html>";
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

// ================= SETUP BLINDADO =================
void setup() {
    Serial.begin(115200);
    pinMode(LED_PIN, OUTPUT); digitalWrite(LED_PIN, HIGH);
    
    // 1. ESPERA INICIAL DE SEGURIDAD (Estabiliza Voltaje)
    delay(1000); 

    // 2. CONFIGURAR PINES
    pinMode(Vext, OUTPUT);
    pinMode(RST_OLED, OUTPUT);

    // 3. SECUENCIA "FUERZA BRUTA" PARA OLED
    // Ciclo 1: Purgar energía residual
    digitalWrite(Vext, HIGH); delay(300);  // OFF (Corta alimentación)
    digitalWrite(Vext, LOW);  delay(500);  // ON (Alimenta)
    
    // Ciclo 2: Asegurar encendido
    digitalWrite(Vext, HIGH); delay(300);  // OFF
    digitalWrite(Vext, LOW);  delay(500);  // ON Final
    
    // Reset Agresivo (Doble golpe)
    digitalWrite(RST_OLED, LOW);  delay(300);
    digitalWrite(RST_OLED, HIGH); delay(300);
    digitalWrite(RST_OLED, LOW);  delay(300); 
    digitalWrite(RST_OLED, HIGH); delay(300);
    
    // 4. INICIALIZAR LIBRERÍA
    screen.init();

    // 5. CHEQUEO Y REINTENTO (Si falló la primera, insiste)
    // Usamos getStringWidth como test rápido. Si devuelve 0, algo va mal.
    if (screen.getStringWidth("T") == 0) { 
        Serial.println("WARN: OLED falló 1er intento. Reintentando...");
        digitalWrite(Vext, HIGH); delay(200); // Apaga rápido
        digitalWrite(Vext, LOW);  delay(500); // Enciende
        digitalWrite(RST_OLED, LOW); delay(500); // Reset largo
        digitalWrite(RST_OLED, HIGH); delay(500);
        screen.init();
    }
    
    screen.flipScreenVertically();
    screen.setFont(ArialMT_Plain_10);
    screen.clear(); screen.drawString(0,0,"BOOT V41 OK"); screen.display();
    // ================= FIN SETUP OLED =================

    pinMode(PRG_BUTTON, INPUT_PULLUP); delay(500); 
    if(digitalRead(PRG_BUTTON) == LOW) configMode = true;

    preferences.begin("conf", false); 
    stored_access_code = preferences.getString("c", "");
    printer_serial = preferences.getString("ser", "0309DA520500162");
    preferences.end();
    
    WiFi.disconnect(true); WiFi.mode(WIFI_AP); WiFi.softAP(ssid_ap_default, NULL, 6, 0, 4);
    
    server.on("/", [](){ server.send(200, "text/html", getHtml()); });
    server.on("/save", [](){
        if(server.hasArg("code")){
            preferences.begin("conf", false); 
            preferences.putString("c", server.arg("code")); preferences.putString("ser", server.arg("serial"));
            preferences.end(); server.send(200, "text/html", "Guardado"); delay(500); ESP.restart();
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
    digitalWrite(LED_PIN, LOW); // LED se apaga al terminar setup
}

void loop() {
    server.handleClient();
    if(configMode) { screen.clear(); screen.drawString(0,0,"MODO CONFIG"); screen.display(); return; }
    
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
        // Latido cardíaco (Flash LED)
        digitalWrite(LED_PIN, HIGH); delay(50); digitalWrite(LED_PIN, LOW);
    }
    Radio.IrqProcess();
}

void reconnect() {
    String id = "E41-"+String(random(0xffff),HEX);
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

void updateOled() {
    screen.clear(); 
    screen.setTextAlignment(TEXT_ALIGN_LEFT); screen.setFont(ArialMT_Plain_10);
    screen.drawString(0,0,"IP:.1 | "+printer_ip);
    screen.drawString(0,15,"N:"+String(temp_nozzle)+" B:"+String(temp_bed));
    screen.setFont(ArialMT_Plain_16);
    screen.drawString(0,40,String(print_percent)+"% "+print_status.substring(0,6));
    screen.display();
}
