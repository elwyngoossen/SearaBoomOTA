#include "WiFi.h"
#include "Audio.h"
#include "ESPAsyncWebServer.h"
#include "AsyncTCP.h"
#include "LittleFS.h"
#include <DNSServer.h>

#define I2S_DOUT        2  // connect to DAC pin DIN
#define I2S_BCLK        3  // connect to DAC pin BCK
#define I2S_LRC         4  // connect to DAC pin LCK
#define TOUCHUP         T7   // connect to touch input 1 (volume up)
#define TOUCHDOWN       T6  // connect to touch input 2 (volume down)
#define TOUCHTHRESS     1500   // touch sensor thresshold (normallay 4000)
#define DEBOUNCE        250    // touch sensor debounce time in milliseconds
#define URL1             "https://radioseara.fm/stream102"  // Stream URL #1 (1027 Stream)
#define URL2             "https://radioseara.fm/stream104"  // Stream URL #2 (1047 Stream)
#define WIFI_TIMEOUT    500   // Wifi timeout before starting AP
#define DEFAULT_VOLUME  5     // Default volume

IPAddress localIP(4, 3, 2, 1);         // Wifi Setup Mode Information
IPAddress localGateway(4, 3, 2, 1);
IPAddress subnet(255,255,255,0);

const String localIPURL = "http://4.3.2.1";	
const char* ssidPath = "/ssid.txt"; // filenames for local files.
const char* passPath = "/pass.txt";
const char* urlPath = "/url.txt";
//int defcontime = (100000); // Delay on setup page. If delay elapses, unit will try again to connect using stored credentials.
int wificonnecttimer = 0; // Counter x 0.5 seconds of attempting to connect to wifi.
int volume = DEFAULT_VOLUME; // starting volume variable
unsigned long lastDebounceTime = 0; // Touch input debounce variable
bool VolumeUpState = false;     // Status variables used for volume up/down interrupts
bool VolumeDownState = false;
unsigned long ledblinker = 0;// timer variable used for led blink
bool ledState = false; // used to determine if LED is on or off
String ssid; // variables to read files into
String password; 
String url;

// Web Page Variables
String html1 = "<!DOCTYPE html><html lang=\"en\"><head><meta charset=\"UTF-8\"><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\"><meta http-equiv=\"X-UA-Compatible\" content=\"ie=edge\"><title>SearaBoom Configuração</title><link rel=\"stylesheet\" href=\"styles.css\"></head><body><article><form action=\"/\" method=\"POST\"><h1><img src=\"background.png\"></h1><fieldset><fieldset><legend>Estação</legend><select name=\"url\" id=\"URL\"><option value=\"URL1\">Nova Russas (FM 102.7)</option><option value=\"URL2\">Ibiapina (FM 104.7)</option></select></fieldset><fieldset><legend>Configurações do WiFi</legend><label for=\"SSID\">Nome:</label><select name=\"ssid\" id=\"SSID\">";
String html2 = "</select><label for=\"PASS\">Senha:</label><input name=\"pass\" id=\"PASS\" type=\"Text\" placeholder=\"Senha Do WiFi\"></fieldset><button type=\"submit\">Salvar</button></form></article></body></html>";

AsyncWebServer server(80);
DNSServer dnsServer;
Audio audio;

void setUpDNSServer(DNSServer &dnsServer, const IPAddress &localIP) {
// Define the DNS interval in milliseconds between processing DNS requests
#define DNS_INTERVAL 30
	// Set the TTL for DNS response and start the DNS server
	dnsServer.setTTL(3600);
	dnsServer.start(53, "*", localIP);
}

void VolumeUp() {                                 // Volume Up subroutine tied to hardware interrupt.
    VolumeUpState = true;
}

void VolumeDown() {                               // Volume Down subroutine tied to hardware interrupt.
    VolumeDownState = true;
}

String readFile(fs::FS &fs, const char * path){             // File Reading Function
    //Serial.printf("Reading file: %s\n", path);
    File file = fs.open(path);
    if(!file || file.isDirectory()){
        //Serial.println("Failed to open file for reading");
        return "";
    }
    char msg[128];
    memset(msg,0,sizeof(msg));
    uint8_t i = 0;
    while(file.available()){
        msg[i] = file.read(); 
        i++;
    }
    String result = String(msg);
    //Serial.println(result);
    file.close();
    return result;
}

void writeFile(fs::FS &fs, const char *path, const char *message) {         // File Writing Function
  //Serial.printf("Writing file: %s\r\n", path);

  File file = fs.open(path, FILE_WRITE);
  if (!file) {
    //Serial.println("- failed to open file for writing");
    return;
  }
  if (file.print(message)) {
    //Serial.println("- file written");
  } else {
    //Serial.println("- write failed");
  }
  file.close();
}

void blinkled(int color,int time){                                      // LED Controller Function
    if (millis() - ledblinker >= time) {
        ledblinker = millis();
        if (ledState == false) {
            ledState = true;
            if (color == 1){
                rgbLedWrite(RGB_BUILTIN, 255, 0, 0);
            }
            if (color == 2){
                rgbLedWrite(RGB_BUILTIN, 0, 255, 0);
            } 
            if (color == 3) {
                rgbLedWrite(RGB_BUILTIN, 0, 0, 255);
            }
        } else {
            ledState = false;
            rgbLedWrite(RGB_BUILTIN, 0, 0, 0);
        }
    }
}
void setupMode() {                                                       // Wifi Setup function
    rgbLedWrite(RGB_BUILTIN, 128, 128, 128);
    WiFi.disconnect();
    WiFi.mode(WIFI_OFF);
    WiFi.mode(WIFI_STA);
    delay(100);
    int n = WiFi.scanNetworks(false);
    String html = html1; // Add first part of HTML webpage.
    if (n < 1) {
        html += "<option value=\"Nenhum Rede Encontrado\">Nenhum Rede Encontrado</option>";
    } else {
        for (int i = 0; i < n; ++i) {           // This builds the HTML for the available networks dropdown
             html += "<option value=\"";
             html += WiFi.SSID(i).c_str();
             html += "\">";
             html += WiFi.SSID(i).c_str();
             if (WiFi.RSSI(i) <= -85){
                html += " (sinal fraco)";
             }
             html += "</option>";
        }
    }
    html += html2; // Add last part of html webpage.
    WiFi.scanDelete();
    WiFi.disconnect();
    WiFi.softAPConfig(localIP,localGateway,subnet);
    WiFi.softAP("SearBoomSetup", NULL);
    // Web Server Root URL
    server.on("/", HTTP_GET, [html](AsyncWebServerRequest *request){
      request->send(200, "text/html", html);
    });
    
    server.serveStatic("/", LittleFS, "/");
    
    server.on("/", HTTP_POST, [](AsyncWebServerRequest *request) {
      const int params = request->params();
      for(int i=0;i<params;i++){
        const AsyncWebParameter* p = request->getParam(i);
        if(p->isPost()){
          // HTTP POST ssid value
          if (p->name() == "ssid") {
            ssid = p->value().c_str();
            // Write file to save value
            writeFile(LittleFS, ssidPath, ssid.c_str());
          }
          // HTTP POST pass value
          if (p->name() == "pass") {
            password = p->value().c_str();
            // Write file to save value
            writeFile(LittleFS, passPath, password.c_str());
          }
           // HTTP POST URL value
          if (p->name() == "url") {
            url = p->value().c_str();
            // Write file to save value
            writeFile(LittleFS, urlPath, url.c_str());
          }
        }
      }
      request->send(200, "text/plain", "Done. Will restart");
      delay(3000);
      ESP.restart();
    });
    server.onNotFound([](AsyncWebServerRequest *request) {
		request->redirect(localIPURL);
    });
    setUpDNSServer(dnsServer, localIP);
    server.begin();
    while (true){
        blinkled(3,500);
        dnsServer.processNextRequest();
	    delay(DNS_INTERVAL);		
    }
}
void setup() {
    Serial.begin(9600);
    Serial.println("Starting...");
    rgbLedWrite(RGB_BUILTIN, 128, 128, 128);
    if(!LittleFS.begin()){
        //Serial.println("An Error has occurred while mounting LittleFS");
        return;
    }
    ssid = readFile(LittleFS, ssidPath); // read wifi network and password into variables
    Serial.print("SSID: ");
    Serial.println(ssid);
    password = readFile(LittleFS, passPath);
    url = readFile(LittleFS, urlPath);
    Serial.print("URL: ");
    Serial.println(url);
    WiFi.begin(ssid, password);         // connect to wifi
    while (WiFi.status() != WL_CONNECTED){
        blinkled(1,500);
        delay(50);   
        wificonnecttimer += 1;
        if (wificonnecttimer > WIFI_TIMEOUT){
            setupMode();
        }          
    } 
    Serial.println("Connected to WiFi");
    audio.setPinout(I2S_BCLK, I2S_LRC, I2S_DOUT);
    audio.setVolume(volume);
    touchAttachInterrupt(TOUCHDOWN, VolumeDown, TOUCHTHRESS); 
    touchAttachInterrupt(TOUCHUP, VolumeUp, TOUCHTHRESS);       // Init touch interrupts
    Serial.println("Connecting to stream");
    if (url == "URL1"){
        audio.connecttohost(URL1);    // Radio Seara 1027 AAC Stream
    } else {
        audio.connecttohost(URL2);    // Radio Seara 1047 AAC Stream
    }
}

void loop() {
    blinkled(2,500);
    audio.loop();
    vTaskDelay(1);
    if (VolumeUpState) {                                            // Handles volume up interrupt (as defined in lines 54-59)
        if ((millis() - lastDebounceTime) > DEBOUNCE){
            if (volume < 23){
                volume = ++volume;
                audio.setVolume(volume);
                lastDebounceTime = millis();
            }
        }
        if (!touchInterruptGetLastStatus(TOUCHUP)) {
            VolumeUpState = false;
        }
    }
    if (VolumeDownState) {                                          // Handles volume down interrrupt (as defined in lines 54-59)
        if ((millis() - lastDebounceTime) > DEBOUNCE){
            if (volume > 1){
                volume = --volume;
                audio.setVolume(volume);
                lastDebounceTime = millis();
            }
        }
        if (!touchInterruptGetLastStatus(TOUCHDOWN)) {
            VolumeDownState = false;
        }
    }
}

