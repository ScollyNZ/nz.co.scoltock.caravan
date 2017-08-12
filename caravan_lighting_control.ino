#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <ArduinoOTA.h>
#include <FS.h>
#include <Hash.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <SPIFFSEditor.h>
#include <StackArray.h>
#include <ArduinoJson.h>

// SKETCH BEGIN
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");
AsyncEventSource events("/events");
AsyncWebSocketClient *currentClient;

void onWsEvent(AsyncWebSocket * server, AsyncWebSocketClient * client, AwsEventType type, void * arg, uint8_t *data, size_t len){
  if(type == WS_EVT_CONNECT){
    Serial.printf("ws[%s][%u] connect. clients=%u\n", server->url(), client->id());
    client->printf("Hello Client %u :)", client->id());
    client->ping();
  } else if(type == WS_EVT_DISCONNECT){
    Serial.printf("ws[%s][%u] disconnect: %u,clients=%u\n", server->url(), client->id());
  } else if(type == WS_EVT_ERROR){
    Serial.printf("ws[%s][%u] error(%u): %s\n", server->url(), client->id(), *((uint16_t*)arg), (char*)data);
  } else if(type == WS_EVT_PONG){
    Serial.printf("ws[%s][%u] pong[%u]: %s\n", server->url(), client->id(), len, (len)?(char*)data:"");
  } else if(type == WS_EVT_DATA){
    AwsFrameInfo * info = (AwsFrameInfo*)arg;
    String msg = "";
    if(info->final && info->index == 0 && info->len == len){
      //the whole message is in a single frame and we got all of it's data
      Serial.printf("ws[%s][%u] %s-message[%llu]: ", server->url(), client->id(), (info->opcode == WS_TEXT)?"text":"binary", info->len);

      if(info->opcode == WS_TEXT){
        for(size_t i=0; i < info->len; i++) {
          msg += (char) data[i];
        }
      } else {
        char buff[3];
        for(size_t i=0; i < info->len; i++) {
          sprintf(buff, "%02x ", (uint8_t) data[i]);
          msg += buff ;
        }
      }
      Serial.printf("%s\n",msg.c_str());

      if(info->opcode == WS_TEXT){
        server->textAll(msg.c_str());
        changeIOState(msg.c_str());
      }
      else
        client->binary("I got your binary message");
    } else {
      //message is comprised of multiple frames or the frame is split into multiple packets
      if(info->index == 0){
        if(info->num == 0)
          Serial.printf("ws[%s][%u] %s-message start\n", server->url(), client->id(), (info->message_opcode == WS_TEXT)?"text":"binary");
        Serial.printf("ws[%s][%u] frame[%u] start[%llu]\n", server->url(), client->id(), info->num, info->len);
      }

      Serial.printf("ws[%s][%u] frame[%u] %s[%llu - %llu]: ", server->url(), client->id(), info->num, (info->message_opcode == WS_TEXT)?"text":"binary", info->index, info->index + len);

      if(info->opcode == WS_TEXT){
        for(size_t i=0; i < info->len; i++) {
          msg += (char) data[i];
        }
      } else {
        char buff[3];
        for(size_t i=0; i < info->len; i++) {
          sprintf(buff, "%02x ", (uint8_t) data[i]);
          msg += buff ;
        }
      }
      Serial.printf("%s\n",msg.c_str());

      if((info->index + len) == info->len){
        Serial.printf("ws[%s][%u] frame[%u] end[%llu]\n", server->url(), client->id(), info->num, info->len);
        if(info->final){
          Serial.printf("ws[%s][%u] %s-message end\n", server->url(), client->id(), (info->message_opcode == WS_TEXT)?"text":"binary");
          if(info->message_opcode == WS_TEXT){
            client->text("I got your text message");
          }
          else
            client->binary("I got your binary message");
        }
      }
    }
  }
}


const char* ssid = "scoltock";
const char* password = "";
const char * hostName = "caravan";
const char* http_username = "admin";
const char* http_password = "admin";
const char css[] PROGMEM =\
"    .onoffswitch { \
        position: relative; width: 180px; \
        -webkit-user-select:none; -moz-user-select:none; -ms-user-select: none; \
    } \
    .onoffswitch-checkbox { \
        display: none; \
    } \
    .onoffswitch-label { \
        display: block; overflow: hidden; cursor: pointer; \
        border: 2px solid ; border-radius: 11px; \
    } \
    .onoffswitch-inner { \
        display: block; width: 200%; margin-left: -100%; \
        transition: margin 0.3s ease-in 0s; \
    } \
    .onoffswitch-inner:before, .onoffswitch-inner:after { \
        display: block; float: left; width: 50%; height: 67px; padding: 0; line-height: 67px; \
        font-size: 40px; color: white; font-family: Trebuchet, Arial, sans-serif; font-weight: bold; \
        box-sizing: border-box; \
    } \
    .onoffswitch-inner:before { \
        content: \"ON\"; \
        padding-left: 21px; \
        background-color: #34A7C1; color: #FFFFFF; \
    } \
    .onoffswitch-inner:after { \
        content: \"OFF\"; \
        padding-right: 21px; \
        background-color: #EEEEEE; color: #999999; \
        text-align: right; \
    } \
    .onoffswitch-switch { \
        display: block; width: 48px; margin: 9.5px; \
        background: #FFFFFF; \
        position: absolute; top: 0; bottom: 0; \
        right: 109px; \
        border: 2px solid ; border-radius: 11px; \
        transition: all 0.3s ease-in 0s;  \
    } \
    .onoffswitch-checkbox:checked + .onoffswitch-label .onoffswitch-inner { \
        margin-left: 0; \
    } \
    .onoffswitch-checkbox:checked + .onoffswitch-label .onoffswitch-switch { \
        right: 0px;  \
    }";

const char html[] PROGMEM =\
"<html>\
  <head>\
    <title>Scoltock's Caravan</title>\
    <link rel=\"stylesheet\" type=\"text/css\" href=\"css\">\
    <style>\
      body { background-color: #cccccc; font-family: Arial, Helvetica, Sans-Serif; Color: #000088; }\
    </style>\
      <script>\
           var socket;\
\
           function begin() {\
               socket = new WebSocket('ws://' + window.location.hostname + '/ws','blah');\
\
            socket.addEventListener('message', function (event) {\
            if (event.data.charAt(0)==\"{\") {\
                lightStatus = JSON.parse(event.data);\
                document.getElementById(lightStatus.light).checked = lightStatus.state;\
                }\
            });\
            }\
\
\
           function changeStatus(control) {\
                var lightStatus={};\
                lightStatus.light=control.id;\
                lightStatus.state=control.checked;\
                socket.send(JSON.stringify(lightStatus));\
                }\
      </script>\
  </head>\
  <body onload=\"begin()\">\
    <h1>Lighting Control</h1>\
    <div style=\"width:600px\">\
       <h2>Kitchen</h2>\
       <div class=\"onoffswitch\"> <input type=\"checkbox\" name=\"kitchen\" class=\"onoffswitch-checkbox\" id=\"kitchen\" onchange=\"changeStatus(this)\">\
          <label class=\"onoffswitch-label\" for=\"kitchen\">\
             <span class=\"onoffswitch-inner\"></span>\
             <span class=\"onoffswitch-switch\"></span>\
          </label>\
       </div>\
       <h2>Double Bed</h2>\
       <div class=\"onoffswitch\"> <input type=\"checkbox\" name=\"doublebed\" class=\"onoffswitch-checkbox\" id=\"doublebed\" onchange=\"changeStatus(this)\">\
          <label class=\"onoffswitch-label\" for=\"doublebed\">\
             <span class=\"onoffswitch-inner\"></span>\
             <span class=\"onoffswitch-switch\"></span>\
          </label>\
       </div>\
       <h2>Bunks</h2>\
       <div style=\"float:left;width:100%\">\
         <div style=\"float:left\">\
           <h3 style=\"float:left\">left</h3>\
           <div style=\"float:right\" class=\"onoffswitch\"><input type=\"checkbox\" name=\"bunk-left\" class=\"onoffswitch-checkbox\" id=\"bunk-left\"  onchange=\"changeStatus(this)\">\
              <label class=\"onoffswitch-label\" for=\"bunk-left\">\
                 <span class=\"onoffswitch-inner\"></span>\
                 <span class=\"onoffswitch-switch\"></span>\
              </label>\
           </div>\
         </div>\
         <div style=\"float:right\">\
           <h3 style=\"float:left\">right</h3>\
           <div style=\"float:right\" class=\"onoffswitch\"><input type=\"checkbox\" name=\"bunk-right\" class=\"onoffswitch-checkbox\" id=\"bunk-right\"  onchange=\"changeStatus(this)\">\
              <label class=\"onoffswitch-label\" for=\"bunk-right\">\
                 <span class=\"onoffswitch-inner\"></span>\
                 <span class=\"onoffswitch-switch\"></span>\
              </label>\
           </div>\
         </div>\
       </div>\
       <h2>Tables</h2>\
       <div style=\"float:left\">\
          <h3 style=\"float:left\">front</h3>\
          <div style=\"float:right\" class=\"onoffswitch\"><input type=\"checkbox\" name=\"table-front\" class=\"onoffswitch-checkbox\" id=\"table-front\"  onchange=\"changeStatus(this)\">\
            <label class=\"onoffswitch-label\" for=\"table-front\">\
               <span class=\"onoffswitch-inner\"></span>\
               <span class=\"onoffswitch-switch\"></span>\
            </label>\
         </div>\
       </div>\
       <div style=\"float:right\">\
       <h3 style=\"float:left\">rear</h3>\
         <div style=\"float:right\" class=\"onoffswitch\"><input type=\"checkbox\" name=\"table-rear\" class=\"onoffswitch-checkbox\" id=\"table-rear\"  onchange=\"changeStatus(this)\">\
            <label class=\"onoffswitch-label\" for=\"table-rear\">\
               <span class=\"onoffswitch-inner\"></span>\
               <span class=\"onoffswitch-switch\"></span>\
            </label>\
         </div>\
       <div>\
    </div>\
  </body>\
</html>";


void changeIOState(String message)
{
  DynamicJsonBuffer jsonBuffer;

  JsonObject& root = jsonBuffer.parseObject(message);

  String state = root[String("state")];
  String light = root[String("light")];

  Serial.printf("Change State of %s to %s",state.c_str(),light.c_str());
  if (light=="kitchen")
    digitalWrite(D0,state=="true"?HIGH:LOW);
  if (light=="bunk-right")
    digitalWrite(D1,state=="true"?HIGH:LOW);
}


void setup(){
  Serial.begin(115200);
  Serial.setDebugOutput(true);
  WiFi.hostname(hostName);
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(hostName);
  WiFi.begin(ssid, password);
  //if (WiFi.waitForConnectResult() != WL_CONNECTED) {
  //  Serial.printf("STA: Failed!\n");
  //  WiFi.disconnect(false);
  //  delay(1000);
  //  WiFi.begin(ssid, password);
  //}

  MDNS.addService("http","tcp",80);

  SPIFFS.begin();

  ws.onEvent(onWsEvent);
  server.addHandler(&ws);

  events.onConnect([](AsyncEventSourceClient *client){
    client->send("hello!",NULL,millis(),1000);
  });
  server.addHandler(&events);

  server.addHandler(new SPIFFSEditor(http_username,http_password));

  server.on("/heap", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(200, "text/plain", String(ESP.getFreeHeap()));
  });
  server.on("/send",HTTP_GET,[](AsyncWebServerRequest *request){
      ws.textAll("boo!");
      request->send(200,"text/plain","Done");
  });

  server.on("/",HTTP_GET,[](AsyncWebServerRequest *request){
    request->send(200, "text/html", FPSTR(html));
  });

  server.on("/css",HTTP_GET,[](AsyncWebServerRequest *request){
    request->send(200, "text/css", FPSTR(css));
  });

  server.serveStatic("/", SPIFFS, "/").setDefaultFile("index.htm");

  server.onNotFound([](AsyncWebServerRequest *request){
    Serial.printf("NOT_FOUND: ");
    if(request->method() == HTTP_GET)
      Serial.printf("GET");
    else if(request->method() == HTTP_POST)
      Serial.printf("POST");
    else if(request->method() == HTTP_DELETE)
      Serial.printf("DELETE");
    else if(request->method() == HTTP_PUT)
      Serial.printf("PUT");
    else if(request->method() == HTTP_PATCH)
      Serial.printf("PATCH");
    else if(request->method() == HTTP_HEAD)
      Serial.printf("HEAD");
    else if(request->method() == HTTP_OPTIONS)
      Serial.printf("OPTIONS");
    else
      Serial.printf("UNKNOWN");
    Serial.printf(" http://%s%s\n", request->host().c_str(), request->url().c_str());

    if(request->contentLength()){
      Serial.printf("_CONTENT_TYPE: %s\n", request->contentType().c_str());
      Serial.printf("_CONTENT_LENGTH: %u\n", request->contentLength());
    }

    int headers = request->headers();
    int i;
    for(i=0;i<headers;i++){
      AsyncWebHeader* h = request->getHeader(i);
      Serial.printf("_HEADER[%s]: %s\n", h->name().c_str(), h->value().c_str());
    }

    int params = request->params();
    for(i=0;i<params;i++){
      AsyncWebParameter* p = request->getParam(i);
      if(p->isFile()){
        Serial.printf("_FILE[%s]: %s, size: %u\n", p->name().c_str(), p->value().c_str(), p->size());
      } else if(p->isPost()){
        Serial.printf("_POST[%s]: %s\n", p->name().c_str(), p->value().c_str());
      } else {
        Serial.printf("_GET[%s]: %s\n", p->name().c_str(), p->value().c_str());
      }
    }

    request->send(404);
  });
  server.onFileUpload([](AsyncWebServerRequest *request, const String& filename, size_t index, uint8_t *data, size_t len, bool final){
    if(!index)
      Serial.printf("UploadStart: %s\n", filename.c_str());
    Serial.printf("%s", (const char*)data);
    if(final)
      Serial.printf("UploadEnd: %s (%u)\n", filename.c_str(), index+len);
  });
  server.onRequestBody([](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total){
    if(!index)
      Serial.printf("BodyStart: %u\n", total);
    Serial.printf("%s", (const char*)data);
    if(index + len == total)
      Serial.printf("BodyEnd: %u\n", total);
  });
  server.begin();

  pinMode(D0, OUTPUT);
  pinMode(D1, OUTPUT);
  pinMode(D2,INPUT_PULLUP);
}

void loop(){
  if (buttonWasPressed(D2))
    {
      Serial.printf("button pressed\n");
      String newLightState = digitalRead(D0)==LOW?"true":"false";
      String message = String("{\"light\":\"kitchen\",\"state\":" + newLightState + "}");
      changeIOState(message.c_str());
      ws.textAll(message.c_str());
    }

}

int lastButtonPressTime = 0;
int lastButtonState = HIGH;
const int debounceDelay = 100;
bool alreadyTriggered = false;

bool buttonWasPressed(int buttonPin){
  int buttonState = digitalRead(buttonPin);

  if (buttonState != lastButtonState) {
    lastButtonState = buttonState;
    lastButtonPressTime = millis();
    alreadyTriggered = false;
  }

  if (!alreadyTriggered)
    if ((millis() - lastButtonPressTime) > debounceDelay) {
      alreadyTriggered = (buttonState==LOW);
      return alreadyTriggered;
    }
return false;
}



