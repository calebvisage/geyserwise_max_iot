#include <ESP8266WiFi.h>
#include <DNSServer.h>
#include <PubSubClient.h>
#include <WiFiManager.h>
#include <ArduinoJson.h>

// JSON configuration file
#define JSON_CONFIG_FILE "/config.json"

constexpr char MQTT_TOPIC_PREFIX[] = "stat/%s/%s";
constexpr char MQTT_COMMAND_PREFIX[] = "cmnd/%s/%s";
constexpr char MQTT_TELE_PREFIX[] = "tele/%s/%s";

char mqtt_server[20] = "";
char mqtt_port[10] = "1883";
char mqtt_user[50] = "admin";
char mqtt_pass[50] = "";
char mqtt_topic[15] = "geyser";

char pump_topic[50];
char temp_topic[50];
char collector_temp_topic[50];
char geyser_topic[50];
char heat_topic[50];
char away_topic[50];
char solar_diff_topic[50];
char anti_freeze_topic[50];
char block1_topic[50];
char block2_topic[50];
char block3_topic[50];
char block4_topic[50];

char mqtt_subscribe_cmd[50];
char mqtt_lwt_topic[50];

char solar_diff_cmnd_topic[50];
char anti_freeze_cmnd_topic[50];
char block1_cmnd_topic[50];
char block2_cmnd_topic[50];
char block3_cmnd_topic[50];
char block4_cmnd_topic[50];
char power_cmnd_topic[50];
char away_cmnd_topic[50];
char wifi_cmnd_topic[50];

WiFiClient espClient;
PubSubClient client(espClient);
WiFiManager wifiManager;

int mqttRetryCount = 0;

//String header;
bool shouldSaveConfig = false;
bool gotMCUResponse = false;

void saveConfigCallback() {
  Serial.println("Should save config");
  shouldSaveConfig = true;
}

#define TUYA_MAX_LEN 98    // Max length of message value
#define TUYA_BUFFER_LEN 6  // Length of serial buffer for header + type + length
#define TUYA_HEADER_LEN 2  // Length of fixed header

//Wifi modes (AP-pairing, connected status, disconnected status)
#define NETWORK_CONNECTION_MODE_PAIRING \
  { 0x01 }
#define NETWORK_CONNECTION_MODE_DISCONNECTED \
  { 0x02 }
#define NETWORK_CONNECTION_MODE_CONNECTED \
  { 0x04 }
/**
   All the commands to set geyser
*/
#define HEAT \
  { 0x01, 0x01, 0x00, 0x01, 0x00 }  //dpid = 1, type = enum, len = 1, value = HEAT
#define HOLIDAY_MODE \
  { 0x02, 0x04, 0x00, 0x01, 0x00 }

/**
   Need to make degrees dynamic, last byte
*/
#define SOLAR_DIFF \
  { 0x66, 0x02, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00 }  //+ 1 byte (0x00)
#define ANTI_FREEZE \
  { 0x6b, 0x02, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00 }  //Set to 0 degrees
#define BLOCK_1 \
  { 0x67, 0x02, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00 }  //+ 1 byte (0x00)
#define BLOCK_2 \
  { 0x68, 0x02, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00 }  //+ 1 byte (0x00)
#define BLOCK_3 \
  { 0x69, 0x02, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00 }  //+ 1 byte (0x00)
#define BLOCK_4 \
  { 0x6a, 0x02, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00 }  //+ 1 byte (0x00)

uint8_t pump = 0;
uint8_t heating = 0;
uint8_t activeMode = 0;
uint8_t geyserTemp = 0;
uint8_t solarDiffTemp = 0;
uint8_t block1 = 0;
uint8_t block2 = 0;
uint8_t block3 = 0;
uint8_t block4 = 0;
uint8_t antiFreezeTemp = 0;
uint8_t collectorTemp = 0;
uint8_t elementHours = 0;
const char *geyserErrorCode = "";

static const uint16_t TUYA_HEADER = 0x55AA;
static const uint16_t TUYA_VERSION = 0x00;

static uint8_t heat[] = HEAT;
static uint8_t holiday_mode[] = HOLIDAY_MODE;
static uint8_t solar_diff[] = SOLAR_DIFF;
static uint8_t anti_freeze[] = ANTI_FREEZE;
static uint8_t block_1[] = BLOCK_1;
static uint8_t block_2[] = BLOCK_2;
static uint8_t block_3[] = BLOCK_3;
static uint8_t block_4[] = BLOCK_4;


static uint8_t network_connection_mode_pairing[] = NETWORK_CONNECTION_MODE_PAIRING;
static uint8_t network_connection_mode_connected[] = NETWORK_CONNECTION_MODE_CONNECTED;
static uint8_t network_connection_mode_disconnected[] = NETWORK_CONNECTION_MODE_DISCONNECTED;


#define HEARTBEAT_INTERVAL_MS 10000
unsigned long previousHeartbeatMillis = 0;

#define QUERY_INTERVAL_MS 1000
unsigned long previousQueryMillis = 0;

#define WIFI_CHECK_INTERVAL_MS 12000
unsigned long previousWifiCheckMillis = 0;

WiFiManagerParameter *custom_mqtt_server;
WiFiManagerParameter *custom_mqtt_port;
WiFiManagerParameter *custom_mqtt_user;
WiFiManagerParameter *custom_mqtt_pass;
WiFiManagerParameter *custom_mqtt_topic;

struct TuyaCommand {
  uint16_t header;
  uint8_t version;
  uint8_t command;
  uint16_t length;
  uint8_t value[TUYA_MAX_LEN];
  uint8_t checksum;
};

enum TuyaCommandType {
  TUYA_HEARTBEAT = 0x00,
  TUYA_WORKING_MODE = 0x02,
  TUYA_NETWORK_STATUS = 0x03,
  TUYA_PAIRING_MODE = 0x04,
  TUYA_PAIRING_MODE_OPTION = 0x05,
  TUYA_COMMAND = 0x06,
  TUYA_RESPONSE = 0x07,
  TUYA_QUERY_STATUS = 0x08
};

TuyaCommand command_{ TUYA_HEADER, TUYA_VERSION, 0, 0, {}, 0 };
uint8_t uart_buffer_[TUYA_BUFFER_LEN]{ 0 };

bool readCommand();
void writeCommand(TuyaCommandType command, const uint8_t *value, uint16_t length);
uint8_t checksum();

//Safe guard for the temp drops in graphs.
uint8_t previousGeyserTemp = 0;
uint8_t previousCollectorTemp = 0;
char checksumStore;

void setup() {
  Serial.begin(9600);

  pinMode(LED_BUILTIN, OUTPUT);

  loadConfigFile();
  setupWifiManager();

  setTopic();
}

void loop() {
  digitalWrite(LED_BUILTIN, HIGH);

  isWifiConnected();

  reconnectMqtt();

  //Starts a web portal so that you can connect to your DHCP IP and still be able to upload new firmware or change wifi settings.
  wifiManager.startWebPortal();
  wifiManager.process();

  unsigned long currentQueryMillis = millis();
  if (currentQueryMillis - previousQueryMillis >= QUERY_INTERVAL_MS) {
    previousQueryMillis += QUERY_INTERVAL_MS;
    Serial.println("Send QUERY");
    writeCommand(TUYA_QUERY_STATUS, 0, 0);
  }

  unsigned long currentHeartbeatMillis = millis();
  if (currentHeartbeatMillis - previousHeartbeatMillis >= HEARTBEAT_INTERVAL_MS) {
    previousHeartbeatMillis += HEARTBEAT_INTERVAL_MS;
    Serial.println("Send HEARTBEAT");
    writeCommand(TUYA_HEARTBEAT, 0, 0);
  }

  bool haveMessage = readCommand();

  //Checks to see if any messages come through
  if (haveMessage && WiFi.status() == WL_CONNECTED) {
    Serial.println("Read Command:");
    Serial.println(command_.command);


    if (command_.command == TUYA_HEARTBEAT) {
      Serial.println("HEARTBEAT_RESPONSE:");
      Serial.println((command_.value[0] == 0) ? "FIRST_HEARTBEAT" : "HEARTBEAT");

    } else if (command_.command == TUYA_RESPONSE && command_.length <= TUYA_MAX_LEN) {
      gotMCUResponse = true;

      // if (checksumStore != command_.checksum) {
      setAndPublishGeyserData(command_.value);

      publishGeyserTemps(command_.value);

      // checksumStore = command_.checksum;
      // }
    }
  }

  if (command_.command == TUYA_PAIRING_MODE || command_.command == TUYA_PAIRING_MODE_OPTION) {
    wifiManager.resetSettings();
    ESP.restart();
  }

  digitalWrite(LED_BUILTIN, LOW);
}

void writeCommand(TuyaCommandType command, const uint8_t *value, uint16_t length) {
  // Copy params into command struct
  command_.header = TUYA_HEADER;
  command_.version = TUYA_VERSION;
  command_.command = command;
  command_.length = length;
  Serial.println("COMMAND:");
  Serial.println("Header:");
  Serial.println(command_.header);
  Serial.println("Version:");
  Serial.println(command_.version);
  Serial.println("Command:");
  Serial.println(command_.command);
  Serial.println("Data length:");
  Serial.println(command_.length);
  memcpy(&command_.value, value, length);
  Serial.println("TX_RAW:");
  for (size_t i = 0; i < command_.length; i++) {
    Serial.println(command_.value[i]);
  }
  // Copy struct values into buffer, converting longs to big-endian
  uart_buffer_[0] = command_.header >> 8;
  uart_buffer_[1] = command_.header & 0xFF;
  uart_buffer_[2] = command_.version;
  uart_buffer_[3] = command_.command;
  uart_buffer_[4] = command_.length >> 8;
  uart_buffer_[5] = command_.length & 0xFF;
  command_.checksum = checksum();
  Serial.println("Checksum:");
  Serial.println(command_.checksum);
  // Send buffer out via UART
  Serial.write(uart_buffer_, TUYA_BUFFER_LEN);
  Serial.write(command_.value, command_.length);
  Serial.write(command_.checksum);
  // Clear buffer contents to avoid re-reading our own payload
  memset(uart_buffer_, 0, TUYA_BUFFER_LEN);
}

bool readCommand() {
  // Shift bytes through until we find a valid header
  bool valid_header = false;
  while (Serial.available() > 0) {
    uart_buffer_[0] = uart_buffer_[1];
    uart_buffer_[1] = Serial.read();
    command_.header = (uart_buffer_[0] << 8) + uart_buffer_[1];
    if (command_.header == TUYA_HEADER) {
      valid_header = true;
      break;
    }
  }

  // Read the next 4 bytes (Version, Command, Data length)
  // Read n bytes (Data length)
  // Read the checksum byte
  if (valid_header) {
    Serial.readBytes(uart_buffer_ + TUYA_HEADER_LEN, TUYA_BUFFER_LEN - TUYA_HEADER_LEN);
    command_.version = uart_buffer_[2];
    command_.command = uart_buffer_[3];
    command_.length = (uart_buffer_[4] << 8) + uart_buffer_[5];

    Serial.println("Command Length:");
    Serial.println(command_.length);

    if (command_.length <= TUYA_MAX_LEN) {
      Serial.readBytes(command_.value, command_.length);

      //      Serial.println("RX_RAW:");
      //      for (size_t i = 0; i < command_.length; i++)
      //      {
      //        Serial.println(command_.value[i]);
      //      }

      while (Serial.available() == 0)  // Dirty
      {
        //Wait
      }
      command_.checksum = Serial.read();
      uint8_t calc_checksum = checksum();
      if (calc_checksum == command_.checksum) {
        // Clear buffer contents to start with beginning of next command
        memset(uart_buffer_, 0, TUYA_BUFFER_LEN);
        return true;
      } else {
        memset(uart_buffer_, 0, TUYA_BUFFER_LEN);
      }
    } else {
      memset(uart_buffer_, 0, TUYA_BUFFER_LEN);
    }
  }

  // Do not clear buffer to allow for resume in case of reading partway through header RX
  return false;
}

uint8_t checksum() {
  uint8_t checksum = 0;
  for (size_t i = 0; i < TUYA_BUFFER_LEN; i++) {
    checksum += uart_buffer_[i];
  }
  for (size_t i = 0; i < command_.length; i++) {
    checksum += command_.value[i];
  }
  return checksum;
}

const char *errorCode(int error) {
  switch (error) {
    case 256:
      return "E9";  //PV Pump failure
    case 128:
      return "E8";  //Collector probe error
    case 64:
      return "E7";  //Comm error
    case 32:
      return "E6";  //Water leak
    case 16:
      return "E5";  //Over temp
    case 8:
      return "E4";  //Heat failure
    case 4:
      return "E3";  //Water tank sensor error
    case 2:
      return "E2";  //Dry burn protection
    case 1:
      return "E1";  //Earth leakage
    default:
      return "";
  }
}

void isWifiConnected() {
  unsigned long currentWifiCheckMillis = millis();
  if (currentWifiCheckMillis - previousWifiCheckMillis >= WIFI_CHECK_INTERVAL_MS) {
    if (WiFi.status() == WL_CONNECTED) {
      writeCommand(TUYA_NETWORK_STATUS, network_connection_mode_connected, sizeof(network_connection_mode_connected));
    } else {
      writeCommand(TUYA_NETWORK_STATUS, network_connection_mode_disconnected, sizeof(network_connection_mode_disconnected));
      WiFi.reconnect();
    }

    previousWifiCheckMillis += WIFI_CHECK_INTERVAL_MS;
  }
}

void setupWifiManager() {
  wifiManager.setDarkMode(true);
  wifiManager.setScanDispPerc(true);
  wifiManager.setHostname("GEYSER_CONTROLLER");

  std::vector<const char *> menu = { "wifi", "info", "custom", "sep", "erase", "update", "restart" };
  wifiManager.setMenu(menu);

  WiFiManagerParameter custom_mqtt_server("server", "mqtt server", mqtt_server, 20);
  WiFiManagerParameter custom_mqtt_port("port", "mqtt port", mqtt_port, 10);
  WiFiManagerParameter custom_mqtt_user("user", "mqtt user", mqtt_user, 50);
  WiFiManagerParameter custom_mqtt_pass("pass", "mqtt pass", mqtt_pass, 50, "type='password'");
  WiFiManagerParameter custom_mqtt_topic("topic", "mqtt topic", mqtt_topic, 15);

  //set config save notify callback
  wifiManager.setSaveConfigCallback(saveConfigCallback);

  //add all your parameters here
  wifiManager.addParameter(&custom_mqtt_server);
  wifiManager.addParameter(&custom_mqtt_port);
  wifiManager.addParameter(&custom_mqtt_user);
  wifiManager.addParameter(&custom_mqtt_pass);
  wifiManager.addParameter(&custom_mqtt_topic);

  bool res;

  wifiManager.setConnectTimeout(180);
  wifiManager.setConnectRetries(100);
  if (wifiManager.getWiFiIsSaved()) wifiManager.setEnableConfigPortal(false);
  res = wifiManager.autoConnect("AP_GEYSER");

  if (res) {
    writeCommand(TUYA_NETWORK_STATUS, network_connection_mode_connected, sizeof(network_connection_mode_connected));
  } else {
    writeCommand(TUYA_NETWORK_STATUS, network_connection_mode_pairing, sizeof(network_connection_mode_pairing));
  }

  strncpy(mqtt_server, custom_mqtt_server.getValue(), sizeof(mqtt_server));
  strncpy(mqtt_port, custom_mqtt_port.getValue(), sizeof(mqtt_port));
  strncpy(mqtt_user, custom_mqtt_user.getValue(), sizeof(mqtt_user));
  strncpy(mqtt_pass, custom_mqtt_pass.getValue(), sizeof(mqtt_pass));
  //Keep the default topic if nothing set.
  if (strlen(custom_mqtt_topic.getValue()) != 0) {
    strncpy(mqtt_topic, custom_mqtt_topic.getValue(), sizeof(mqtt_topic));
  }

  //save the custom parameters to FS
  if (shouldSaveConfig) {
    saveConfigFile();
  }

  Serial.println("local ip");
  Serial.println(WiFi.localIP());
}

void setTopic() {
  //MQTT stat commands
  sprintf(pump_topic, MQTT_TOPIC_PREFIX, mqtt_topic, "PUMP");
  sprintf(temp_topic, MQTT_TOPIC_PREFIX, mqtt_topic, "TEMP");
  sprintf(collector_temp_topic, MQTT_TOPIC_PREFIX, mqtt_topic, "COLLECTOR");
  sprintf(geyser_topic, MQTT_TOPIC_PREFIX, mqtt_topic, "RESULT");
  sprintf(heat_topic, MQTT_TOPIC_PREFIX, mqtt_topic, "POWER");
  sprintf(away_topic, MQTT_TOPIC_PREFIX, mqtt_topic, "AWAY");
  sprintf(solar_diff_topic, MQTT_TOPIC_PREFIX, mqtt_topic, "SOLARDIFF");
  sprintf(anti_freeze_topic, MQTT_TOPIC_PREFIX, mqtt_topic, "ANTIFREEZE");
  sprintf(block1_topic, MQTT_TOPIC_PREFIX, mqtt_topic, "BLOCK1");
  sprintf(block2_topic, MQTT_TOPIC_PREFIX, mqtt_topic, "BLOCK2");
  sprintf(block3_topic, MQTT_TOPIC_PREFIX, mqtt_topic, "BLOCK3");
  sprintf(block4_topic, MQTT_TOPIC_PREFIX, mqtt_topic, "BLOCK4");

  //MQTT subscribe commands
  sprintf(mqtt_subscribe_cmd, MQTT_COMMAND_PREFIX, mqtt_topic, "+");
  sprintf(mqtt_lwt_topic, MQTT_TELE_PREFIX, mqtt_topic, "LWT");

  //MQTT commands
  sprintf(solar_diff_cmnd_topic, MQTT_COMMAND_PREFIX, mqtt_topic, "solar_diff");
  sprintf(anti_freeze_cmnd_topic, MQTT_COMMAND_PREFIX, mqtt_topic, "anti_freeze");
  sprintf(block1_cmnd_topic, MQTT_COMMAND_PREFIX, mqtt_topic, "block1");
  sprintf(block2_cmnd_topic, MQTT_COMMAND_PREFIX, mqtt_topic, "block2");
  sprintf(block3_cmnd_topic, MQTT_COMMAND_PREFIX, mqtt_topic, "block3");
  sprintf(block4_cmnd_topic, MQTT_COMMAND_PREFIX, mqtt_topic, "block4");
  sprintf(power_cmnd_topic, MQTT_COMMAND_PREFIX, mqtt_topic, "power");
  sprintf(away_cmnd_topic, MQTT_COMMAND_PREFIX, mqtt_topic, "away");
  sprintf(wifi_cmnd_topic, MQTT_COMMAND_PREFIX, mqtt_topic, "wifi");
}

void saveConfigFile() {
  Serial.println(F("Saving configuration..."));

  // Create a JSON document
  StaticJsonDocument<512> json;
  json["mqtt_server"] = mqtt_server;
  json["mqtt_port"] = mqtt_port;
  json["mqtt_user"] = mqtt_user;
  json["mqtt_pass"] = mqtt_pass;
  json["mqtt_topic"] = mqtt_topic;

  // Open config file
  File configFile = SPIFFS.open(JSON_CONFIG_FILE, "w");
  if (!configFile) {
    // Error, file did not open
    Serial.println("failed to open config file for writing");
  }

  // Serialize JSON data to write to file
  serializeJsonPretty(json, Serial);
  if (serializeJson(json, configFile) == 0) {
    // Error writing file
    Serial.println(F("Failed to write to file"));
  }
  // Close file
  configFile.close();
}

bool loadConfigFile()
// Load existing configuration file
{
  // Read configuration from FS json
  Serial.println("Mounting File System...");

  // May need to make it begin(true) first time you are using SPIFFS
  if (SPIFFS.begin()) {
    Serial.println("mounted file system");
    if (SPIFFS.exists(JSON_CONFIG_FILE)) {
      // The file exists, reading and loading
      Serial.println("reading config file");
      File configFile = SPIFFS.open(JSON_CONFIG_FILE, "r");
      if (configFile) {
        Serial.println("Opened configuration file");
        StaticJsonDocument<512> json;
        DeserializationError error = deserializeJson(json, configFile);
        serializeJsonPretty(json, Serial);
        if (!error) {
          Serial.println("Parsing JSON");

          strcpy(mqtt_server, json["mqtt_server"]);
          strcpy(mqtt_port, json["mqtt_port"]);
          strcpy(mqtt_user, json["mqtt_user"]);
          strcpy(mqtt_pass, json["mqtt_pass"]);
          strcpy(mqtt_topic, json["mqtt_topic"]);

          return true;
        } else {
          // Error loading JSON data
          Serial.println("Failed to load json config");
        }
      }
    }
  } else {
    Serial.println("** Failed to mount FS **");
  }

  return false;
}


void setAndPublishGeyserData(const uint8_t *geyserValues) {
  pump = geyserValues[33];
  heating = geyserValues[4];
  activeMode = geyserValues[9];
  geyserErrorCode = errorCode(geyserValues[28]);
  solarDiffTemp = geyserValues[41];
  antiFreezeTemp = geyserValues[81];
  block1 = geyserValues[49];
  block2 = geyserValues[57];
  block3 = geyserValues[65];
  block4 = geyserValues[73];
  elementHours = geyserValues[97];


  StaticJsonDocument<200> doc;
  doc["error"] = geyserErrorCode;
  doc["solar_diff_temp"] = solarDiffTemp;
  doc["anti_freeze_temp"] = antiFreezeTemp;
  doc["block1_temp"] = block1;
  doc["block2_temp"] = block2;
  doc["block3_temp"] = block3;
  doc["block4_temp"] = block4;
  doc["element_hours"] = elementHours;

  char buffer[256];
  size_t n = serializeJson(doc, buffer);

  client.publish(geyser_topic, buffer, n);
  client.publish(heat_topic, heating ? "ON" : "OFF");
  client.publish(away_topic, activeMode ? "OFF" : "ON");
  client.publish(pump_topic, pump ? "ON" : "OFF");
}

void publishGeyserTemps(const uint8_t *geyserValues) {
  geyserTemp = geyserValues[17];
  if (previousGeyserTemp > 0 && geyserTemp > 0 && (previousGeyserTemp - geyserTemp) < 3) {
    char tempAsString[32] = { 0 };
    snprintf(tempAsString, sizeof(tempAsString), "%d", geyserTemp);
    client.publish(temp_topic, tempAsString);
  } else {
    previousGeyserTemp = geyserTemp;
  }

  collectorTemp = geyserValues[89];
  if (previousCollectorTemp > 0 && collectorTemp > 0 && (previousCollectorTemp - collectorTemp) < 3) {
    char collectorTempAsString[32] = { 0 };
    snprintf(collectorTempAsString, sizeof(collectorTempAsString), "%d", collectorTemp);
    client.publish(collector_temp_topic, collectorTempAsString);
  } else {
    previousCollectorTemp = collectorTemp;
  }
}

void reconnectMqtt() {
  // Loop until we're reconnected
  while (!client.connected()) {
    if (WiFi.status() == WL_CONNECTED) {
      Serial.print("Attempting MQTT connection...");
      Serial.println("Saved MQTT username:");
      Serial.println(mqtt_user);
      Serial.println("Saved MQTT password:");
      Serial.println(mqtt_pass);
      Serial.println("Saved MQTT server:");
      Serial.println(mqtt_server);
      Serial.println("Saved MQTT port:");
      Serial.println(mqtt_port);

      String clientId = "Client-";
      clientId += String(random(0xffff), HEX);

      client.setServer(mqtt_server, atoi(mqtt_port));
      if (client.connect(clientId.c_str(), mqtt_user, mqtt_pass)) {
        Serial.println("MQTT Connected");
        client.setCallback(callback);
        client.subscribe(mqtt_subscribe_cmd);
        client.publish(mqtt_lwt_topic, "ONLINE");
      } else {
        Serial.println("Failed to connect to MQTT");
        Serial.println(" try again in 5 seconds");
        Serial.println("MQTT Count:");
        Serial.println(mqttRetryCount);

        delay(5000);
        mqttRetryCount++;

        if (mqttRetryCount == 50) {
          Serial.println("Reached maximum MQTT retry count. Resetting WiFi settings and restarting.");
          wifiManager.resetSettings();
          ESP.restart();
        }
      }
    }
  }
  client.loop();
}

char *toCharArray(String str) {
  return &str[0];
}

void callback(char *topic, byte *payload, unsigned int length) {
  Serial.println("Got a command from home assistant");
  Serial.println();
  payload[length] = '\0';
  String message = (char *)payload;
  int tempVal = message.toInt();

  //Only enable writing commands if the MCU has responded after initial query else it throws everything out of sync with bad request order.
  if (gotMCUResponse) {
    if (strcmp(topic, solar_diff_cmnd_topic) == 0) {
      if (tempVal >= 7 && tempVal <= 15) {
        solar_diff[7] = tempVal;
        client.publish(solar_diff_topic, toCharArray(message));
        writeCommand(TUYA_COMMAND, solar_diff, sizeof(solar_diff));
      }
    }

    if (strcmp(topic, anti_freeze_cmnd_topic) == 0) {
      if (tempVal >= 0 && tempVal <= 10) {
        anti_freeze[7] = tempVal;
        client.publish(anti_freeze_topic, toCharArray(message));
        writeCommand(TUYA_COMMAND, anti_freeze, sizeof(anti_freeze));
      }
    }

    if (strcmp(topic, block1_cmnd_topic) == 0) {
      if (tempVal >= 30 && tempVal <= 65) {
        block_1[7] = tempVal;
        client.publish(block1_topic, toCharArray(message));
        writeCommand(TUYA_COMMAND, block_1, sizeof(block_1));
      }
    }

    if (strcmp(topic, block2_cmnd_topic) == 0) {
      if (tempVal >= 30 && tempVal <= 65) {
        block_2[7] = tempVal;
        client.publish(block2_topic, toCharArray(message));
        writeCommand(TUYA_COMMAND, block_2, sizeof(block_2));
      }
    }

    if (strcmp(topic, block3_cmnd_topic) == 0) {
      if (tempVal >= 30 && tempVal <= 65) {
        block_3[7] = tempVal;
        client.publish(block3_topic, toCharArray(message));
        writeCommand(TUYA_COMMAND, block_3, sizeof(block_3));
      }
    }

    if (strcmp(topic, block4_cmnd_topic) == 0) {
      if (tempVal >= 30 && tempVal <= 65) {
        block_4[7] = tempVal;
        client.publish(block4_topic, toCharArray(message));
        writeCommand(TUYA_COMMAND, block_4, sizeof(block_4));
      }
    }

    if (strcmp(topic, power_cmnd_topic) == 0) {
      if (message == "ON") {
        heat[4] = 1;
        writeCommand(TUYA_COMMAND, heat, sizeof(heat));
      }
      if (message == "OFF") {
        heat[4] = 0;
        writeCommand(TUYA_COMMAND, heat, sizeof(heat));
      }
    }
    if (strcmp(topic, away_cmnd_topic) == 0) {
      if (message == "ON") {
        holiday_mode[4] = 0;
        client.publish(away_topic, "ON");
        writeCommand(TUYA_COMMAND, holiday_mode, sizeof(holiday_mode));
      }
      if (message == "OFF") {
        holiday_mode[4] = 1;
        client.publish(heat_topic, "OFF");
        writeCommand(TUYA_COMMAND, holiday_mode, sizeof(holiday_mode));
      }
    }
  }

  //Reset the wifi settings if button press reset doesnt work
  if (strcmp(topic, wifi_cmnd_topic) == 0) {
    Serial.println("Resetting the wifi config");

    wifiManager.resetSettings();
    ESP.restart();
  }
}
