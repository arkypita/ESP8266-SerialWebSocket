#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <WebSocketsServer.h>    //https://github.com/Links2004/arduinoWebSockets/issues/61
#include <Hash.h>
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPUpdateServer.h>
#include <WiFiManager.h>         //https://github.com/tzapu/WiFiManager


// Transparent Serial Bridge code from Marcus https://github.com/Links2004/arduinoWebSockets/issues/61

ESP8266WebServer httpServer(80);
ESP8266HTTPUpdateServer httpUpdater;
const char* update_path = "/firmware";
const char* update_username = "admin";
const char* update_password = "admin";


WebSocketsServer webSocket = WebSocketsServer(81);


WiFiManager wifiManager;
int RESET_PIN = 0; // = GPIO0 on nodeMCU
bool socketConnected = false;

#define SEND_SERIAL_TIME (50)

class SerialTerminal {
    public:
        void setup() {
            _lastRX = 0;
            resetBuffer();
            Serial.begin(115200);
        }

        void loop() {
            unsigned long t = millis();
            bool forceSend = false;

            size_t len = (_bufferWritePtr - &_buffer[0]);
            int free = (sizeof(_buffer) - len);

            int available = Serial.available();
            if(available > 0 && free > 0) {
                int readBytes = available;
                if(readBytes > free) {
                    readBytes = free;
                }
                readBytes = Serial.readBytes(_bufferWritePtr, readBytes);
                _bufferWritePtr += readBytes;
                _lastRX = t;
            }

            // check for data in buffer
            len = (_bufferWritePtr - &_buffer[0]);
            if(len >=  sizeof(_buffer)) {
                forceSend = true;
            }
            if(len > (WEBSOCKETS_MAX_HEADER_SIZE + 1)) {
                if(((t - _lastRX) > SEND_SERIAL_TIME) || forceSend) {
                    webSocket.broadcastTXT(&_buffer[0], (len - WEBSOCKETS_MAX_HEADER_SIZE), true);
                    resetBuffer();
                }
            }
        }


    protected:
        uint8_t _buffer[1460];
        uint8_t * _bufferWritePtr;
        unsigned long _lastRX;

        void resetBuffer() {
            // offset for adding Websocket header
            _bufferWritePtr = &_buffer[WEBSOCKETS_MAX_HEADER_SIZE];
            // addChar('T');
        }

        inline void addChar(char c) {
            *_bufferWritePtr = (uint8_t) c; // message type for Webinterface
            _bufferWritePtr++;
        }
};

SerialTerminal term;

void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t lenght) 
{
	if (type == WStype_DISCONNECTED)
	{
		socketConnected = false;
	}
	else if (type == WStype_CONNECTED)
 	{
		socketConnected = true;
		webSocket.sendTXT(num, "Connected\n");
	}
	else if (type == WStype_TEXT)
	{
		if(lenght > 0)
		{
			String command = String((const char *)payload);
			command.toUpperCase();
            if (command == "WIFIRESET\n")
            {
            	webSocket.sendTXT(num, "ok\n");  
                webSocket.sendTXT(num, "Resetting WiFi settings!\n");
                delay(500);   
				wifiManager.resetSettings();
                delay(100);
                ESP.restart();
            }
            else if (command == "WIFISTATUS\n")
            {
				webSocket.sendTXT(num, "ok\n");  
            	
            	char buffer[100];      //declare array
                IPAddress local = WiFi.localIP();
                IPAddress gatew = WiFi.gatewayIP();
              
                SendFormat(num, "Connected to:    %s\n", WiFi.SSID().c_str());
				SendFormat(num, "Signal strength: %ddBm\n", WiFi.RSSI());
                SendFormat(num, "Local IP:        %d.%d.%d.%d\n", local[0], local[1], local[2], local[3]);
                SendFormat(num, "Gateway IP:      %d.%d.%d.%d\n", gatew[0], gatew[1], gatew[2], gatew[3]);
            }
            else if (command == "CHIPSTATUS\n")
            {
				webSocket.sendTXT(num, "ok\n"); 
				uint32_t realSize = ESP.getFlashChipRealSize();
				uint32_t ideSize = ESP.getFlashChipSize();
				FlashMode_t ideMode = ESP.getFlashChipMode();

				SendFormat(num,"CPU Freq:        %uMHz\n", ESP.getCpuFreqMHz());
				SendFormat(num,"Flash speed:     %uMHz\n", ESP.getFlashChipSpeed()/1000000);
				SendFormat(num,"Flash real size: %uKB\n", realSize/1024);
				SendFormat(num,"Flash ide  size: %uKB\n", ideSize/1024);
				SendFormat(num,"Flash ide  mode: %s\n", (ideMode == FM_QIO ? "QIO" : ideMode == FM_QOUT ? "QOUT" : ideMode == FM_DIO ? "DIO" : ideMode == FM_DOUT ? "DOUT" : "UNKNOWN"));
				SendFormat(num,"Flash ID:        %08X\n", ESP.getFlashChipId());
            }
            else
            {
           		Serial.write((const char *) (payload), (lenght));
            }
		}
	}

}

void SendFormat (uint8_t num, char * format, ...)
{
	char buffer[100];
	va_list args;
	va_start (args, format);
	vsnprintf (buffer, 100, format, args);
	va_end (args);
	webSocket.sendTXT(num, buffer);	
}

void setup()
{
    delay(5000); //BOOT WAIT
    pinMode(RESET_PIN, INPUT_PULLUP);
    wifiManager.autoConnect("ESP8266");

    webSocket.begin();
    webSocket.onEvent(webSocketEvent);

    httpUpdater.setup(&httpServer, update_path, update_username, update_password);
    httpServer.begin();

    term.setup();

    WiFi.setSleepMode(WIFI_NONE_SLEEP); // disable WiFi sleep for more performance
}


void loop()
{
    term.loop();
    webSocket.loop();
    httpServer.handleClient();
}


