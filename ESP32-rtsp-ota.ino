//
#define ENABLE_OLED //if want use oled ,turn on thi macro
// #define SOFTAP_MODE // If you want to run our own softap turn this on
#define ENABLE_WEBSERVER
#define ENABLE OTASERVER
#define ENABLE_RTSPSERVER
//#define CAMERA_MODULE_OV2640
#define CAM_NAME "front-hall"
#define CAMERA_MODEL_AI_THINKER

#include <WiFi.h>
#include <WebServer.h>
#include <WiFiClient.h>
#include "SimStreamer.h"
#include "OV2640.h"
#include "OV2640Streamer.h"
#include "CRtspSession.h"
#include "camera_pins.h"

#include <Update.h>


#ifdef SOFTAP_MODE
IPAddress apIP = IPAddress(192, 168, 1, 2);
#else
#include "wifikeys_herr.h"
//#include "wifikeys_oland.h"
//#include "wifikeys_jk.h"
//#include "wifikeys_sk.h"
#endif

// A Name for the Camera. (can be set in myconfig.h)
#ifdef CAM_NAME
  char myName[] = CAM_NAME;
#else
  char myName[] = "ESP32 camera server";
#endif

// Status and illumination LED's
#ifdef LAMP_PIN 
  int lampVal = 0; // Current Lamp value, range 0-100, Start off
#else 
  int lampVal = -1; // disable Lamp
#endif         
int lampChannel = 7;     // a free PWM channel (some channels used by camera)
const int pwmfreq = 50000;     // 50K pwm frequency
const int pwmresolution = 9;   // duty cycle bit range
// https://diarmuid.ie/blog/pwm-exponential-led-fading-on-arduino-or-other-platforms
const int pwmIntervals = 100;  // The number of Steps between the output being on and off
float lampR;                   // The R value in the PWM graph equation (calculated in setup)


// This will be displayed to identify the firmware
char myVer[] PROGMEM = "ESP32-sk-ota " __DATE__ " @ " __TIME__;


OV2640 cam;
esp_err_t errCam  = ESP_ERR_INVALID_STATE;
esp_err_t rtspStat = ESP_ERR_INVALID_STATE;

#ifdef ENABLE_RTSPSERVER
WiFiServer rtspServer(8554);
CStreamer *streamer;
#endif

#ifdef ENABLE OTASERVER
/*
 * Login page
 */
const char* loginIndex = 
 "<form name='loginForm'>"
    "<table width='20%' bgcolor='A09F9F' align='center'>"
        "<tr>"
            "<td colspan=2>"
                "<center><font size=4><b>ESP32 Login Page</b></font></center>"
                "<br>"
            "</td>"
            "<br>"
            "<br>"
        "</tr>"
        "<td>Username:</td>"
        "<td><input type='text' size=25 name='userid'><br></td>"
        "</tr>"
        "<br>"
        "<br>"
        "<tr>"
            "<td>Password:</td>"
            "<td><input type='Password' size=25 name='pwd'><br></td>"
            "<br>"
            "<br>"
        "</tr>"
        "<tr>"
            "<td><input type='submit' onclick='check(this.form)' value='Login'></td>"
        "</tr>"
    "</table>"
"</form>"
"<script>"
    "function check(form)"
    "{"
    "if(form.userid.value=='admin' && form.pwd.value=='admin')"
    "{"
    "window.open('/serverIndex')"
    "}"
    "else"
    "{"
    " alert('Error Password or Username')/*displays error message*/"
    "}"
    "}"
"</script>";

/*
 * Server Index Page
 */
 
const char* serverIndex = 
"<script src='https://ajax.googleapis.com/ajax/libs/jquery/3.2.1/jquery.min.js'></script>"
"<form method='POST' action='#' enctype='multipart/form-data' id='upload_form'>"
   "<input type='file' name='update'>"
        "<input type='submit' value='Update'>"
    "</form>"
 "<div id='prg'>progress: 0%</div>"
 "<script>"
  "$('form').submit(function(e){"
  "e.preventDefault();"
  "var form = $('#upload_form')[0];"
  "var data = new FormData(form);"
  " $.ajax({"
  "url: '/update',"
  "type: 'POST',"
  "data: data,"
  "contentType: false,"
  "processData:false,"
  "xhr: function() {"
  "var xhr = new window.XMLHttpRequest();"
  "xhr.upload.addEventListener('progress', function(evt) {"
  "if (evt.lengthComputable) {"
  "var per = evt.loaded / evt.total;"
  "$('#prg').html('progress: ' + Math.round(per*100) + '%');"
  "}"
  "}, false);"
  "return xhr;"
  "},"
  "success:function(d, s) {"
  "console.log('success!')" 
 "},"
 "error: function (a, b, c) {"
 "}"
 "});"
 "});"
 "</script>";
 
#endif

String dbg = "dbg";

#ifdef ENABLE_WEBSERVER

  WebServer server(80);

  void handle_jpg_stream(void)
  {
      uint32_t now = millis();
      uint32_t streamstart = now;
      WiFiClient client = server.client();
      String response = "HTTP/1.1 200 OK\r\n";
      response += "Content-Type: multipart/x-mixed-replace; boundary=frame\r\n\r\n";
      server.sendContent(response);

      while (1)
      {
          now = millis();
          if( now > streamstart +24*60*60*1000 || now < streamstart) { // handle clock rollover
          break;
          }
          cam.run();
          if (!client.connected())
              break;
          response = "--frame\r\n";
          response += "Content-Type: image/jpeg\r\n\r\n";
          server.sendContent(response);

          client.write((char *)cam.getfb(), cam.getSize());
          server.sendContent("\r\n");
          if (!client.connected())
              break;
      }
  }

  void handle_jpg(void)
  {
      WiFiClient client = server.client();

      cam.run();
      if (!client.connected())
      {
          return;
      }
      String response = "HTTP/1.1 200 OK\r\n";
      response += "Content-disposition: inline; filename=capture.jpg\r\n";
      response += "Content-type: image/jpeg\r\n\r\n";
      server.sendContent(response);
      client.write((char *)cam.getfb(), cam.getSize());
  }

  void handle_help()
  {
      uint32_t now = millis();
      String message = "<h2>Server is running!</h2>";
      message += "<p>URI: <b>";
      message += server.uri();
      message += "\n<br>now:";
      message += now;
      message += "/n<br>"+dbg;
      message += "\n<br><a href=\"/\">help: this page</a>";
      message += "\n<br><a href=\"/stream\">stream cam</a><p>( conflikts with RTSP,this isalocking loop untill connection closed</p>";
      message += "\n<br><a href=\"/jpg\">take a jpeg snapshot (not stored)</a></p>";
      message += "<h3>/blixt?m=[On/Off]{&val=[0..255]}: current value=";
      message += lampVal;
      message += "</h3>";
      message += "\n<a href=\"/blixt?m=Off\">Off</a> <b>";
      message += "<a href=\"/blixt?m=On&val=15\">on-low</a> <b>";
      message += "<a href=\"/blixt?m=On&val=50\">on-50</a> <b>";
      message += "<a href=\"/blixt?m=On&val=100\">on-max</a>";
      message += "\n<br><a href=\"/reboot\">reboot</a>";
      message += "\n<Br>Esp_cam_status:";
      message += errCam;
      message += "="+ ESP_OK;
      message += "\n<Br>Esp_rtsp_status:";
      message += rtspStat;
      message += "\n<br><a href=\"/reinitCam\">reinitCam</a>";
      message += "\n<br><a href=\"/serverIndex\">update bin</a>";
      message += "<p> server.args=<b>";
      message += server.args();
      message += "\n<br>";
      for (uint8_t i = 0; i < server.args(); i++) {
         message += " " + server.argName(i) + ": " + server.arg(i) + "\n<br>";
      }
      server.send(200, "text/html", message);
  }
  void handle_blixt()
  {
    int brightness;
    if (server.args() > 0 ) {
      if ( server.arg(0)= "On" ) {
        int val = server.arg(1).toInt();
        lampVal = val;
        Serial.println("");
        Serial.println("Lamp On: "+ lampVal);
        if (lampVal > 100) lampVal = 100;  // normalise 0-255 (pwm range) just in case..
        if (lampVal < 0 ) lampVal = 0;
        // https://diarmuid.ie/blog/pwm-exponential-led-fading-on-arduino-or-other-platforms
        brightness = pow (2, (lampVal / lampR)) - 1;
        Serial.print("Lamp: ");
        ledcWrite(lampChannel, brightness);
        Serial.print(lampVal);
        Serial.print("%, pwm = ");
        Serial.println(brightness);

      } else {
        lampVal = 0; 
        brightness = pow (2, (lampVal / lampR)) - 1;
        ledcWrite(lampChannel, brightness);
        Serial.print("Lamp Off: "+lampVal);
        Serial.print("%, pwm = ");
        Serial.println(brightness);
      }
    }
      String message = "Server is running!\n\n";
      message += "Lamp: " + lampVal;
      message += "%, pwm = " + brightness;
      message += "/nURI: "+ server.uri() + " Arguments: " +server.args() + "\n";
      for (uint8_t i = 0; i < server.args(); i++) {
         message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
         Serial.println(i + " " + server.argName(i) + ": " + server.arg(i) );
      }
      server.send(200, "text/plain", message);
  }
  void handleNotFound()
  {
      String message = "Server is running!\n\n";
      message += "URI: ";
      message += server.uri();
      message += "\nMethod: ";
      message += (server.method() == HTTP_GET) ? "GET" : "POST";
      message += "\nArguments: ";
      message += server.args();
      message += "\n";
      for (uint8_t i = 0; i < server.args(); i++) {
         message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
         Serial.print( i);
         Serial.println(" " + server.argName(i) + ": " + server.arg(i) );
      }
      server.send(200, "text/plain", message);
  }
  void handleReboot()
  {
    Serial.print(" Http requested reboot");
    delay(100);
    ESP.restart();
  }
  void handleReInitEspCam(){
    initEspCam();
    String message = "EspCam reply";
    message+= errCam;
    message += "\n";
    server.send(200, "text/plain", message);
  }
#endif

void initEspCam(){
    esp32cam_aithinker_config.ledc_channel = LEDC_CHANNEL_0;
    esp32cam_aithinker_config.ledc_timer = LEDC_TIMER_0;
    uint8_t retry = 0;
    while (errCam != ESP_OK) {
      if (retry < 3) {
        errCam = cam.init(esp32cam_aithinker_config);
        dbg+=".";
        if (errCam != ESP_OK) {
          Serial.printf("Camera init failed with error 0x%x", errCam);
          retry = retry + 1;
          delay(1000);
        } else {
          Serial.printf("Camera init reply 0x%x", errCam);
          dbg+=" Eok ";
        }
      } else {
        return;
      }
    }
}

void setup()
{
    Serial.begin(115200);
    while (!Serial){;}

#ifdef LED_PIN  // If we have a notification LED set it to output
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LED_OFF); 
#endif

#ifdef LAMP_PIN
  ledcSetup(lampChannel, pwmfreq, pwmresolution); // configure LED PWM channel
  ledcWrite(lampChannel, lampVal);                // set initial value
  ledcAttachPin(LAMP_PIN, lampChannel);           // attach the GPIO pin to the channel 
  // Calculate the PWM scaling R factor: 
  lampR = (pwmIntervals * log10(2))/(log10(pow(2,pwmresolution)));
#endif
    dbg+=" first ";
    initEspCam();
    
    IPAddress ip;

    Serial.print("Connect to ");
    Serial.print(ssid);
    flashLED(200);
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED)
    {
        delay(500);
        Serial.print(F("."));
    }
    flashLED(400);
    delay(100);
    flashLED(200);

    ip = WiFi.localIP();
    Serial.println("");
    Serial.print(F("WiFi connected"));
    Serial.print(" ");
    Serial.println(ip);
    dbg+=ip;
    dbg+=" ";

#ifdef ENABLE OTASERVER
    server.on("/login", HTTP_GET, []() {
	    server.sendHeader("Connection", "close");
      server.send(200, "text/html", loginIndex);
    });
  server.on("/serverIndex", HTTP_GET, []() {
    server.sendHeader("Connection", "close");
    server.send(200, "text/html", serverIndex);
  });
  /*handling uploading firmware file */
  server.on("/update", HTTP_POST, []() {
    server.sendHeader("Connection", "close");
    server.send(200, "text/plain", (Update.hasError()) ? "FAIL" : "OK");
    ESP.restart();
  }, []() {
    HTTPUpload& upload = server.upload();
    if (upload.status == UPLOAD_FILE_START) {
      Serial.printf("Update: %s\n", upload.filename.c_str());
      if (!Update.begin(UPDATE_SIZE_UNKNOWN)) { //start with max available size
        Update.printError(Serial);
      }
    } else if (upload.status == UPLOAD_FILE_WRITE) {
      /* flashing firmware to ESP*/
      if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
        Update.printError(Serial);
      }
    } else if (upload.status == UPLOAD_FILE_END) {
      if (Update.end(true)) { //true to set the size to the current progress
        Serial.printf("Update Success: %u\nRebooting...\n", upload.totalSize);
      } else {
        Update.printError(Serial);
      }
    }
  });

#endif


#ifdef ENABLE_WEBSERVER
    server.on("/", HTTP_GET, handle_help);
    server.on("/blixt", HTTP_GET, handle_blixt);
    server.on("/stream", HTTP_GET, handle_jpg_stream);
    server.on("/jpg", HTTP_GET, handle_jpg);
    server.on("/reboot", HTTP_GET, handleReboot);
    server.on("/reinitCam", HTTP_GET, handleReInitEspCam);
    server.onNotFound(handleNotFound);
    server.begin();
#endif

#ifdef ENABLE_RTSPSERVER
    dbg+=" Ertsp ";
    dbg+= ( errCam == 0 );
    if ( errCam == 0 ) {
      dbg+=";";
      rtspServer.begin();
      streamer = new OV2640Streamer(cam);             // our streamer for UDP/TCP based RTP transport
      rtspStat = ESP_OK;
      dbg+=" rtspOK ";
    } else {
      dbg +=" rtspelse " ;
    }
    dbg+=" rtspE ";
#endif

    lampVal = 0;
    ledcWrite(lampChannel, pow (2, (lampVal / lampR)) - 1);
    Serial.print("Lamp: 0");
dbg+="startup_end ";

} // startup end

// Notification LED 
void flashLED(int flashtime)
{
#ifdef LED_PIN                    // If we have it; flash it.
  digitalWrite(LED_PIN, LED_ON);  // On at full power.
  delay(flashtime);               // delay
  digitalWrite(LED_PIN, LED_OFF); // turn Off
#else
  return;                         // No notifcation LED, do nothing, no delay
#endif
}
void loop()
{
#ifdef ENABLE_WEBSERVER
    server.handleClient();
#endif
    if( millis() > 24*60*60*1000 ) {
       ESP.restart();
    }

#ifdef ENABLE_RTSPSERVER
    if ( errCam == ESP_OK ) {
     
      if ( rtspStat != ESP_OK ) {
          dbg+=":";
          rtspServer.begin();
          dbg+="a";
          streamer = new OV2640Streamer(cam);             // our streamer for UDP/TCP based RTP transport
          dbg+="b";
          rtspStat = ESP_OK;
      }
    

    uint32_t msecPerFrame = 100;
    static uint32_t lastimage = millis();
    static int counter = 0;

    // If we have an active client connection, just service that until gone
    streamer->handleRequests(0); // we don't use a timeout here,
    // instead we send only if we have new enough frames
    uint32_t now = millis();
    uint32_t now1 = now;
    if ( streamer->anySessions() ) {
        if ( now > lastimage + msecPerFrame || now < lastimage ) { // handle clock rollover
            streamer->streamImage(now);
            uint32_t now2 = millis();
            uint32_t now3 = lastimage;
            lastimage = now;

            // check if we are overrunning our max frame rate
            now = now2;
            if ( now > lastimage + msecPerFrame ) {
                printf("warning exceeding max frame rate of %d ms\n", now - lastimage);
            }
            if ( counter < 1 ) {
              counter = 9;  
              printf("RTSP statistics : %d , %d , %d ms delta: %d, %d , %d \n",now1,now2,now,now2-now1,now-now2,now-now3);
            } else {
              --counter;
            }
        }
    }
    
    WiFiClient rtspClient = rtspServer.accept();
    if( rtspClient ) {
        Serial.print("client: ");
        Serial.print(rtspClient.remoteIP());
        Serial.println();
        streamer->addSession(rtspClient);
    }
    }
#endif
}
