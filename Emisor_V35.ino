/* CODIGO EMISOR V35 - CNC BRIDGE & GCODE TRANSLATOR */
/* Función: Conecta con Bambu, recibe datos y ESCUCHA órdenes de movimiento del Receptor V37 */

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

// CONFIGURACIÓN MQTT
const char* ssid_ap_default = "HP_Emisor_V35";     
// IMPORTANTE: Asegúrate de que este es tu SERIAL. Si no, cámbialo en la web de config.
String printer_serial = "0309DA520500162"; 

// HARDWARE HELTEC V3
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
WiFiClientSecure espClient; 
PubSubClient client(espClient);
WebServer server(80);
Preferences preferences; 
static RadioEvents_t RadioEvents;

// ESTADO
bool configMode = false;
String printer_ip = ""; 
bool printer_found = false;
String stored_access_code = "";
int lora_profile = 2; 
int lora_power = 14;

// DATOS IMPRESORA
int print_percent=0; int time_remaining=0; String print_status="OFF";
int temp_nozzle=0; int temp_bed=0; int layer_num=0; int total_layer_num=0; int fan_speed=0;
const int BUFFER_SIZE = 20480; 
char jsonBuffer[BUFFER_SIZE]; 

// ================== TRADUCTOR DE G-CODE (LA MAGIA) ==================
void sendMqttCommand(String action) {
    if(!client.connected()) return;
    
    String jsonPayload = "";
    String topic_pub = "device/" + printer_serial + "/request";
    
    // 1. COMANDOS SIMPLES
    if(action == "PAUSE") jsonPayload = "{\"print\": {\"sequence_id\": \"0\", \"command\": \"pause\"}}";
    else if(action == "RESUME") jsonPayload = "{\"print\": {\"sequence_id\": \"0\", \"command\": \"resume\"}}";
    else if(action == "STOP") jsonPayload = "{\"print\": {\"sequence_id\": \"0\", \"command\": \"stop\"}}";
    
    // 2. COMANDOS DE LUZ
    else if(action == "LIGHT_ON") jsonPayload = "{\"system\": {\"sequence_id\": \"0\", \"command\": \"ledctrl\", \"led_node\": \"chamber_light\", \"led_mode\": \"on\", \"led_on_time\": 500, \"led_off_time\": 500, \"loop_times\": 0, \"interval_time\": 0}}";
    else if(action == "LIGHT_OFF") jsonPayload = "{\"system\": {\"sequence_id\": \"0\", \"command\": \"ledctrl\", \"led_node\": \"chamber_light\", \"led_mode\": \"off\"}}";
    
    // 3. COMANDOS DE MOVIMIENTO (CNC)
    else if(action == "HOME") {
        // G28: Auto Home
        jsonPayload = "{\"print\": {\"sequence_id\": \"0\", \"command\": \"gcode_line\", \"param\": \"G28\"}}";
    }
    else if(action.startsWith("MOVE:")) {
        // Formato recibido: MOVE:EJE:CANTIDAD (Ej: MOVE:X:10 o MOVE:Z:-10)
        // Usamos G91 (Relativo) -> Movemos -> G90 (Volvemos a Absoluto por seguridad)
        
        char axis = action.charAt(5); // X, Y o Z
        String dist = action.substring(7);
        
        String gcode = "G91 \\n G1 " + String(axis) + dist + " F3000 \\n G90";
        if(axis == 'Z') gcode = "G91 \\n G1 Z" + dist + " F600 \\n G90"; // Z más lento
        
        jsonPayload = "{\"print\": {\"sequence_id\": \"0\", \"command\": \"gcode_line\", \"param\": \"" + gcode + "\"}}";
    }

    if(jsonPayload != "") {
        Serial.println("MQTT OUT: " + jsonPayload);
        client.publish(topic_pub.c_str(), jsonPayload.c_str());
        
        // Feedback en OLED
        screen.clear(); screen.setFont(ArialMT_Plain_16); 
        screen.setTextAlignment(TEXT_ALIGN_CENTER);
        screen.drawString(64,20, "CMD OK"); 
        screen.setFont(ArialMT_Plain_10);
        screen.drawString(64,40, action); screen.display();
    }
}

// ================== LORA RX (ESCUCHAR ORDENES) ==================
void OnRxDone(uint8_t *payload, uint16_t size, int16_t rssi, int8_t snr) {
    char msg[size+1]; memcpy(msg, payload, size); msg[size]=0; String cmd(msg);
    Serial.println("LoRa RX: " + cmd);
    
    if(cmd.startsWith("CMD:")) {
        String action = cmd.substring(4);
        sendMqttCommand(action);
    }
    Radio.Rx(0); // Volver a escuchar inmediatamente
}

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

// ================== WEB SERVER ==================
String getHtml() {
  String h = "<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1'>";
  h += "<style>body{background:#111;color:#eee;font-family:sans-serif;text-align:center;}";
  h += ".box{background:#222;padding:15px;margin:10px auto;border-radius:10px;max-width:400px;border:1px solid #444;}";
  h += "input,select,button{width:100%;padding:12px;margin:5px 0;background:#333;color:white;border:1px solid #555;border-radius:5px;}";
  h += "button{background:#0a0;font-weight:bold;cursor:pointer;}";
  h += ".warn{color:#fa0;font-size:12px;}</style>";
  if(!configMode) h += "<script>setInterval(()=>{fetch('/data').then(r=>r.json()).then(d=>{document.getElementById('s').innerText=d.s; document.getElementById('p').innerText=d.p+'%';})},2000);</script>";
  h += "</head><body>";
  
  if(configMode) h += "<h2 style='color:#fa0'>⚠️ MODO CONFIGURACIÓN</h2>"; else h += "<h2>📡 EMISOR V35 (CNC)</h2>";
  if(!configMode) h += "<div class='box'><h1 id='p'>" + String(print_percent) + "%</h1><p id='s'>" + print_status + "</p></div>";
  
  h += "<div class='box'><h3>⚙️ AJUSTES</h3><form action='/save' method='POST'>";
  h += "<label>Serial Impresora:</label><input type='text' name='serial' value='" + printer_serial + "'>";
  h += "<label>Access Code:</label><input type='text' name='code' value='" + stored_access_code + "'>";
  
  h += "<label>Perfil LoRa:</label><select name='profile'>";
  String opts[] = {"Turbo", "Rápido", "Balanceado", "Largo"};
  for(int i=0; i<4; i++) h += "<option value='"+String(i)+"' "+(lora_profile==i?"selected":"")+">"+opts[i]+"</option>";
  h += "</select>";
  
  h += "<label>Potencia:</label><select name='power'>";
  for(int i=2; i<=20; i+=2) h += "<option value='"+String(i)+"' "+(lora_power==i?"selected":"")+">"+String(i)+" dBm</option>";
  h += "</select><button type='submit'>GUARDAR</button></form></div>";
  
  h += "<div class='box'><h3>📲 OTA</h3><form method='POST' action='/update' enctype='multipart/form-data'><input type='file' name='update'><button style='background:#007bff'>SUBIR .BIN</button></form></div>";
  
  h += "</body></html>";
  return h;
}

void handleUpdate() {
  HTTPUpload& upload = server.upload();
  if (upload.status == UPLOAD_FILE_START) Update.begin(UPDATE_SIZE_UNKNOWN);
  else if (upload.status == UPLOAD_FILE_WRITE) Update.write(upload.buf, upload.currentSize);
  else if (upload.status == UPLOAD_FILE_END) { Update.end(true); server.send(200,"text/html","<h1>OK!</h1>"); delay(1000); ESP.restart(); }
}

void checkPrinter() {
    if(configMode) return;
    WiFiClientSecure p; p.setInsecure(); p.setTimeout(1); 
    // Escaneo inteligente: .2 suele ser el móvil en AP mode, probamos más
    String ips[] = {"192.168.4.2", "192.168.4.3", "192.168.4.4", "192.168.4.5"};
    for(String ip : ips) {
        if(p.connect(ip.c_str(), 8883)) {
            p.stop(); printer_ip=ip; printer_found=true; 
            client.setServer(printer_ip.c_str(), 8883); 
            client.setCallback(callback); return;
        }
    }
    printer_found=false;
}

void setup() {
    Serial.begin(115200);
    pinMode(PRG_BUTTON, INPUT_PULLUP); delay(500); 
    if(digitalRead(PRG_BUTTON) == LOW) configMode = true;

    preferences.begin("conf", false); 
    stored_access_code = preferences.getString("c", "");
    printer_serial = preferences.getString("ser", "0309DA520500162"); // Serial por defecto
    lora_profile = preferences.getInt("prof", 2);
    lora_power = preferences.getInt("pow", 14);
    preferences.end();
    
    pinMode(Vext,OUTPUT); digitalWrite(Vext,HIGH); delay(100); digitalWrite(Vext,LOW); delay(100);               
    pinMode(RST_OLED,OUTPUT); digitalWrite(RST_OLED,LOW); delay(200); digitalWrite(RST_OLED,HIGH); delay(200);   
    screen.init(); screen.setFont(ArialMT_Plain_10); screen.flipScreenVertically();
    
    // MODO AP para configuración inicial y conexión impresora
    WiFi.disconnect(true); WiFi.mode(WIFI_AP); WiFi.softAP(ssid_ap_default, NULL, 6, 0, 4);
    
    server.on("/", [](){ server.send(200, "text/html", getHtml()); });
    server.on("/data", [](){ String j="{\"p\":"+String(print_percent)+",\"s\":\""+print_status+"\"}"; server.send(200,"application/json",j); });
    server.on("/save", [](){
        if(server.hasArg("code")){
            preferences.begin("conf", false); 
            preferences.putString("c", server.arg("code"));
            preferences.putString("ser", server.arg("serial"));
            preferences.putInt("prof", server.arg("profile").toInt()); 
            preferences.putInt("pow", server.arg("power").toInt());
            preferences.end(); 
            server.send(200, "text/html", "Guardado. Reiniciando..."); delay(500); ESP.restart();
        }
    });
    server.on("/update", HTTP_POST, [](){ server.send(200, "text/plain", (Update.hasError()) ? "FAIL" : "OK"); }, handleUpdate);
    server.begin();
    
    SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_CS);
    RadioEvents.TxDone = [](){ Radio.Rx(0); }; // Al terminar TX, volver a RX
    RadioEvents.RxDone = OnRxDone; // Callback para recibir comandos
    Radio.Init(&RadioEvents); Radio.SetChannel(RF_FREQUENCY);
    configLoRa();
    
    espClient.setInsecure(); espClient.setTimeout(5); client.setBufferSize(BUFFER_SIZE); client.setKeepAlive(45);
    
    screen.clear();
    if(configMode) { screen.drawString(0,0, "MODO CONFIG"); screen.drawString(0,20, "Web: 192.168.4.1"); }
    else { screen.drawString(0,0, "EMISOR V35 LISTO"); screen.drawString(0,20, "Esperando Impresora..."); }
    screen.display();
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
        lastMsg=millis(); sendLoRa(); updateOled(); 
    }
    Radio.IrqProcess();
}

void reconnect() {
    String id = "E35-"+String(random(0xffff),HEX);
    if(client.connect(id.c_str(),"bblp",stored_access_code.c_str())) { 
        String topic_sub = "device/" + printer_serial + "/report";
        client.subscribe(topic_sub.c_str()); 
    }
}

void callback(char* topic, byte* payload, unsigned int length) {
    if(length>=BUFFER_SIZE) return;
    memcpy(jsonBuffer, payload, length); jsonBuffer[length]=0;
    StaticJsonDocument<512> f; f["print"]["mc_percent"]=true; f["print"]["mc_remaining_time"]=true;
    f["print"]["gcode_state"]=true; f["print"]["nozzle_temper"]=true; f["print"]["bed_temper"]=true;
    f["print"]["layer_num"]=true; f["print"]["total_layer_num"]=true; f["print"]["fan_gear"]=true;
    StaticJsonDocument<2048> doc; deserializeJson(doc, jsonBuffer, DeserializationOption::Filter(f));
    JsonObject p = doc["print"];
    if(!p.isNull()) {
        if(p.containsKey("mc_percent")) print_percent=p["mc_percent"];
        if(p.containsKey("mc_remaining_time")) time_remaining=p["mc_remaining_time"];
        if(p.containsKey("gcode_state")) print_status=p["gcode_state"].as<String>();
        if(p.containsKey("nozzle_temper")) temp_nozzle=p["nozzle_temper"];
        if(p.containsKey("bed_temper")) temp_bed=p["bed_temper"];
        if(p.containsKey("layer_num")) layer_num=p["layer_num"];
        if(p.containsKey("total_layer_num")) total_layer_num=p["total_layer_num"];
        if(p.containsKey("fan_gear")) fan_speed=map(p["fan_gear"],0,15,0,100);
    }
}

void sendLoRa() {
    char p[100]; snprintf(p,100,"%d|%d|%s|%d|%d|%d|%d|%d",print_percent,time_remaining,print_status.c_str(),temp_nozzle,temp_bed,layer_num,total_layer_num,fan_speed);
    Radio.Send((uint8_t*)p, strlen(p));
}

void updateOled() {
    screen.clear(); 
    screen.setTextAlignment(TEXT_ALIGN_LEFT); screen.setFont(ArialMT_Plain_10);
    screen.drawString(0,0,"IP:.1 | "+printer_ip);
    screen.drawString(0,12,client.connected()?"MQTT OK":"Buscando...");
    screen.drawString(0,25,"N:"+String(temp_nozzle)+" B:"+String(temp_bed)+" F:"+String(fan_speed)+"%");
    screen.setFont(ArialMT_Plain_16);
    screen.drawString(0,40,String(print_percent)+"% "+print_status.substring(0,6));
    screen.display();
}