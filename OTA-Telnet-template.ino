/*
  To upload through terminal you can use: curl -F "image=@firmware.bin" esp8266-webupdate.local/update
*/

#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>

#include <TelnetLogger.h>
#include <DupLogger.h>

#include <credentials.h>


ESP8266WebServer httpServer(80);

TelnetLogger loggerTelnet;
DupLogger logger(Logger::getDefault(), loggerTelnet);
static char hostId[128];

const char* uploadFormHtml = R"=====(
<!DOCTYPE html>
<html>
  <body>
    <form method='POST' action='/update' enctype='multipart/form-data'>
      <input type='file' name='update'>
      <input type='submit' value='Update'>
    </form>
  </body>
</html>
)=====";

void setup(void) {
  pinMode(LED_BUILTIN, OUTPUT);     // LED_BUILTIN pin as an output
  digitalWrite(LED_BUILTIN, LOW);   // LED on

  Serial.begin(115200);
  delay(1000);

   // Set chip ID 
  snprintf(hostId, sizeof(hostId), "ESP-%06X",  ESP.getChipId());
  logger.setContext(hostId);

  logger.info("Application " __FILE__ " compiled " __DATE__ " at " __TIME__ );
  
  WiFi.mode(WIFI_AP_STA);
  WiFi.begin(STASSID, STAPSK);
  
  while (WiFi.status() != WL_CONNECTED) {
    logger.info(".");
    delay(1000);
  }

  MDNS.begin(hostId);
  
  httpServer.on("/", HTTP_GET, []() {
    httpServer.sendHeader("Connection", "close");
    httpServer.send(200, "text/html", uploadFormHtml);
    logger.info("Sent upload form");
  });

  httpServer.on("/update", HTTP_POST, []() {
    httpServer.sendHeader("Connection", "close");
    httpServer.send(200, "text/plain", (Update.hasError()) ? "FAIL" : "OK");
    logger.warn("Update %s", (Update.hasError()) ? "successful" : "failed");
    ESP.restart();
  }, []() {
    HTTPUpload& upload = httpServer.upload();
    if (upload.status == UPLOAD_FILE_START) {
      Serial.setDebugOutput(true);
      WiFiUDP::stopAll();
      
      uint32_t maxSketchSpace = (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
      logger.warn("Updating: %s. Free sketch space %u Bytes, upload size %u Bytes.\n", 
          upload.filename.c_str(), maxSketchSpace, upload.currentSize);
      
      if (!Update.begin(maxSketchSpace)) { //start with max available size
        Update.printError(Serial);
        logger.error("Update begin failed");
      }
    } else if (upload.status == UPLOAD_FILE_WRITE) {
      // TODO: add written = Update.write(...); if (written != upload.currentSize) {... 
      if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
        Update.printError(Serial);
        logger.error("File write failed");
      }
      else 
      {
        logger.info("Received %u bytes", upload.totalSize);
      }
    } else if (upload.status == UPLOAD_FILE_END) {
      if (Update.end(true)) { //true to set the size to the current progress
        Serial.printf("Update Success: %u\nRebooting...\n", upload.totalSize);
        logger.warn("Update successful, %u Bytes written. Now rebooting.", upload.totalSize);
      } else {
        Update.printError(Serial);      
      }
      Serial.setDebugOutput(false);
    }
    yield();
  });
  httpServer.begin();

  MDNS.addService("http", "tcp", 80);

  logger.info("Application " __FILE__ " compiled " __DATE__ " at " __TIME__ );
  logger.info("WebUpdate server is ready! Send sketch update to http://%s.local in your browser, or via curl\n", hostId);
}


void loop(void) {
  String line;
  httpServer.handleClient();
  MDNS.update();
  handleCommands();
  heartbeat();
}

void parseCommand(String command)
{
  logger.info("Got command `%s` (len = %u)", command.c_str(), command.length());
}


void heartbeat() 
{
  static int nextMs = 0;
  const int periodMs = 5000;
  if (millis() >= nextMs)
  {
    digitalWrite(LED_BUILTIN, LOW);   // Turn the LED on
    nextMs += periodMs;
    logger.info("%s on %s at %s, heap %d Bytes free.",
        __FILE__, hostId, WiFi.localIP().toString().c_str(), ESP.getFreeHeap());
     digitalWrite(LED_BUILTIN, HIGH);
  }
}



void handleCommands() 
{
  static char cmdBuf[1024];
  static uint16_t index = 0;
  // if telnet client sends us data, push into buffer until end of line
  while (loggerTelnet._client.available()) 
  {
    int c = loggerTelnet._client.read();
    if (c >= 0) 
    {
      if (c < ' ') // includes '\r', '\n', '\0'
      {
        cmdBuf[index++] = '\0';
        if (index > 0) 
        {
          parseCommand(String(cmdBuf));
        }
        index = 0;
      }
      else
      {
        cmdBuf[index++] = c;
      }
    }
  }
}
