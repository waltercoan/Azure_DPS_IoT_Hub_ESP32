// Copyright (c) Microsoft Corporation. All rights reserved.
// SPDX-License-Identifier: MIT

/*
 * This is an Arduino-based Azure IoT Hub sample for ESPRESSIF ESP32 boards.
 * It uses our Azure Embedded SDK for C to help interact with Azure IoT.
 * For reference, please visit https://github.com/azure/azure-sdk-for-c.
 *
 * To connect and work with Azure IoT Hub you need an MQTT client, connecting, subscribing
 * and publishing to specific topics to use the messaging features of the hub.
 * Our azure-sdk-for-c is an MQTT client support library, helping composing and parsing the
 * MQTT topic names and messages exchanged with the Azure IoT Hub.
 *
 * This sample performs the following tasks:
 * - Synchronize the device clock with a NTP server;
 * - Initialize our "az_iot_hub_client" (struct for data, part of our azure-sdk-for-c);
 * - Initialize the MQTT client (here we use ESPRESSIF's esp_mqtt_client, which also handle the tcp
 * connection and TLS);
 * - Connect the MQTT client (using server-certificate validation, SAS-tokens for client
 * authentication);
 * - Periodically send telemetry data to the Azure IoT Hub.
 *
 * To properly connect to your Azure IoT Hub, please fill the information in the `iot_configs.h`
 * file.
 */

// C99 libraries
#include <cstdlib>
#include <string.h>
#include <time.h>

// Libraries for MQTT client and WiFi connection
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <mqtt_client.h>

// Azure IoT SDK for C includes
#include <az_core.h>
#include <az_iot.h>
#include <azure_ca.h>

// Additional sample headers
#include "AzIoTSasToken.h"
#include "SerialLogger.h"
#include "iot_configs.h"

#include "esp_err.h"
#include "esp_log.h"
#include "esp32-hal-log.h"


#include <stdint.h>
#include <stdlib.h>
#include "esp_heap_caps.h"
#include "esp_transport.h"
#include <ArduinoJson.h>


// When developing for your own Arduino-based platform,
// please follow the format '(ard;<platform>)'.
#define AZURE_SDK_CLIENT_USER_AGENT "c%2F" AZ_SDK_VERSION_STRING "(ard;esp32)"

// Utility macros and defines
#define sizeofarray(a) (sizeof(a) / sizeof(a[0]))
#define NTP_SERVERS "pool.ntp.org", "time.nist.gov"
#define MQTT_QOS1 1
#define DO_NOT_RETAIN_MSG 0
#define SAS_TOKEN_DURATION_IN_MINUTES 60
#define UNIX_TIME_NOV_13_2017 1510592825

#define PST_TIME_ZONE -8
#define PST_TIME_ZONE_DAYLIGHT_SAVINGS_DIFF 1

#define GMT_OFFSET_SECS (PST_TIME_ZONE * 3600)
#define GMT_OFFSET_SECS_DST ((PST_TIME_ZONE + PST_TIME_ZONE_DAYLIGHT_SAVINGS_DIFF) * 3600)

// Translate iot_configs.h defines into variables used by the sample
static const char* ssid = IOT_CONFIG_WIFI_SSID;
static const char* password = IOT_CONFIG_WIFI_PASSWORD;
static const char* host = IOT_CONFIG_IOTHUB_FQDN;
static const char* hostDPS = IOT_CONFIG_DPS_FQDN;
static const char* scope = IOT_CONFIG_DPS_SCOPE;
static const char* registrationid = IOT_CONFIG_DPS_REGISTRATION_ID;
static const char* mqtt_broker_uri = IOT_CONFIG_IOTHUB_FQDN;
static const char* device_id = IOT_CONFIG_DEVICE_ID;
static const int mqtt_port = AZ_IOT_DEFAULT_MQTT_CONNECT_PORT;

// Memory allocated for the sample's variables and structures.
static esp_mqtt_client_handle_t mqtt_client;
static esp_mqtt_client_handle_t mqtt_client_DPS;
static az_iot_hub_client client;
static az_iot_provisioning_client provisioning;

static char mqtt_client_id[128];
static char mqtt_username[128];
static char mqtt_password[200];
static uint8_t sas_signature_buffer[256];
static unsigned long next_telemetry_send_time_ms = 0;
static char telemetry_topic[128];
static uint32_t telemetry_send_count = 0;
static String telemetry_payload = "{}";
static String dps_payload = "{}";
String dps_topic = "";
static const char* operationId;
String topicvalue = "";
String topicdata = "";

#define INCOMING_DATA_BUFFER_SIZE 512
static char incoming_data[INCOMING_DATA_BUFFER_SIZE];

static char mqtt_client_username_buffer[128];
static char mqtt_client_id_buffer[128];

// Auxiliary functions
#ifndef IOT_CONFIG_USE_X509_CERT
static AzIoTSasToken sasToken(
    &client,
    AZ_SPAN_FROM_STR(IOT_CONFIG_DEVICE_KEY),
    AZ_SPAN_FROM_BUFFER(sas_signature_buffer),
    AZ_SPAN_FROM_BUFFER(mqtt_password));
#endif // IOT_CONFIG_USE_X509_CERT

static void connectToWiFi()
{
  Logger.Info("Connecting to WIFI SSID " + String(ssid));

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");

  Logger.Info("WiFi connected, IP address: " + WiFi.localIP().toString());
}

static void initializeTime()
{
  Logger.Info("Setting time using SNTP");

  configTime(GMT_OFFSET_SECS, GMT_OFFSET_SECS_DST, NTP_SERVERS);
  time_t now = time(NULL);
  while (now < UNIX_TIME_NOV_13_2017)
  {
    delay(500);
    Serial.print(".");
    now = time(nullptr);
  }
  Serial.println("");
  Logger.Info("Time initialized!");
}

void receivedCallback(char* topic, byte* payload, unsigned int length)
{
  Logger.Info("Received [");
  Logger.Info(topic);
  Logger.Info("]: ");
  for (int i = 0; i < length; i++)
  {
    Serial.print((char)payload[i]);
  }
  Serial.println("");
}

static esp_err_t mqtt_dps_event_handler(esp_mqtt_event_handle_t event)
{

  
  switch (event->event_id)
  {
    int i, r;

    case MQTT_EVENT_ERROR:
      Logger.Info("MQTT DPS event MQTT_EVENT_ERROR");
      break;
    case MQTT_EVENT_CONNECTED:
      Logger.Info("MQTT DPS event MQTT_EVENT_CONNECTED");

      r = esp_mqtt_client_subscribe(mqtt_client_DPS, "$dps/registrations/res/#", 1);
      if (r == -1)
      {
        Logger.Error("MQTT DPS Could not subscribe for cloud-to-device messages.");
      }
      else
      {
        Logger.Info("MQTT DPS Subscribed message id:" + String(r));
      }

      break;
    case MQTT_EVENT_DISCONNECTED:
      Logger.Info("MQTT DPS event MQTT_EVENT_DISCONNECTED");
      break;
    case MQTT_EVENT_SUBSCRIBED:
      Logger.Info("MQTT DPS event MQTT_EVENT_SUBSCRIBED");
      break;
    case MQTT_EVENT_UNSUBSCRIBED:
      Logger.Info("MQTT DPS event MQTT_EVENT_UNSUBSCRIBED");
      break;
    case MQTT_EVENT_PUBLISHED:
      Logger.Info("MQTT DPS event MQTT_EVENT_PUBLISHED");
      break;
    case MQTT_EVENT_DATA:
      Logger.Info("MQTT DPS event MQTT_EVENT_DATA");

      for (i = 0; i < (INCOMING_DATA_BUFFER_SIZE - 1) && i < event->topic_len; i++)
      {
        incoming_data[i] = event->topic[i];
      }
      incoming_data[i] = '\0';
      Logger.Info("Topic: " + String(incoming_data));
      topicvalue = String(incoming_data);

      for (i = 0; i < (INCOMING_DATA_BUFFER_SIZE - 1) && i < event->data_len; i++)
      {
        incoming_data[i] = event->data[i];
      }
      incoming_data[i] = '\0';
      Logger.Info("Data: " + String(incoming_data));
      topicdata = String(incoming_data);
      Serial.println(topicdata);

      if(topicvalue.startsWith("$dps/registrations/res/202/")){
        Logger.Info("PARSING JSON ");
          DynamicJsonDocument doc(2048);
          DeserializationError error = deserializeJson(doc, topicdata);

          if (error) {
            Serial.print(F("deserializeJson() failed: "));
            Serial.println(error.f_str());
          }
          String _operationId = doc["operationId"];
          operationId = _operationId.c_str();
      }
      if(topicvalue.startsWith("$dps/registrations/res/200/")){
        Logger.Info("PARSING JSON ");
          DynamicJsonDocument doc(2048);
          DeserializationError error = deserializeJson(doc, topicdata);

          if (error) {
            Serial.print(F("deserializeJson() failed: "));
            Serial.println(error.f_str());
          }
          String assignedHub = doc["registrationState"]["assignedHub"];
          host = assignedHub.c_str();
      }
      

      break;
    case MQTT_EVENT_BEFORE_CONNECT:
      Logger.Info("MQTT DPS event MQTT_EVENT_BEFORE_CONNECT");
      break;
    default:
      Logger.Error("MQTT DPS event UNKNOWN");
      break;
  }

  return ESP_OK;
}


static esp_err_t mqtt_event_handler(esp_mqtt_event_handle_t event)
{
  switch (event->event_id)
  {
    int i, r;

    case MQTT_EVENT_ERROR:
      Logger.Info("MQTT event MQTT_EVENT_ERROR");
      break;
    case MQTT_EVENT_CONNECTED:
      Logger.Info("MQTT event MQTT_EVENT_CONNECTED");

      r = esp_mqtt_client_subscribe(mqtt_client, AZ_IOT_HUB_CLIENT_C2D_SUBSCRIBE_TOPIC, 1);
      if (r == -1)
      {
        Logger.Error("Could not subscribe for cloud-to-device messages.");
      }
      else
      {
        Logger.Info("Subscribed for cloud-to-device messages; message id:" + String(r));
      }

      break;
    case MQTT_EVENT_DISCONNECTED:
      Logger.Info("MQTT event MQTT_EVENT_DISCONNECTED");
      break;
    case MQTT_EVENT_SUBSCRIBED:
      Logger.Info("MQTT event MQTT_EVENT_SUBSCRIBED");
      break;
    case MQTT_EVENT_UNSUBSCRIBED:
      Logger.Info("MQTT event MQTT_EVENT_UNSUBSCRIBED");
      break;
    case MQTT_EVENT_PUBLISHED:
      Logger.Info("MQTT event MQTT_EVENT_PUBLISHED");
      break;
    case MQTT_EVENT_DATA:
      Logger.Info("MQTT event MQTT_EVENT_DATA");

      for (i = 0; i < (INCOMING_DATA_BUFFER_SIZE - 1) && i < event->topic_len; i++)
      {
        incoming_data[i] = event->topic[i];
      }
      incoming_data[i] = '\0';
      Logger.Info("Topic: " + String(incoming_data));

      for (i = 0; i < (INCOMING_DATA_BUFFER_SIZE - 1) && i < event->data_len; i++)
      {
        incoming_data[i] = event->data[i];
      }
      incoming_data[i] = '\0';
      Logger.Info("Data: " + String(incoming_data));

      break;
    case MQTT_EVENT_BEFORE_CONNECT:
      Logger.Info("MQTT event MQTT_EVENT_BEFORE_CONNECT");
      break;
    default:
      Logger.Error("MQTT event UNKNOWN");
      break;
  }

  return ESP_OK;
}

static void initializeIoTHubClient()
{

  /*CODIGO DE REGISTRO NO DPS*/
  Logger.Info("az_result_failed(az_iot_provisioning_client_init");
  az_iot_provisioning_client_options provisioning_options = az_iot_provisioning_client_options_default();
  if (az_result_failed(az_iot_provisioning_client_init(
    &provisioning,
    az_span_create((uint8_t*)hostDPS, strlen(hostDPS)),
    az_span_create((uint8_t*)scope, strlen(scope)),
    az_span_create((uint8_t*)registrationid, strlen(registrationid)),
    NULL
  )))
  {
    Logger.Error("Failed DPS");
    return;
  }
  
  Logger.Info("az_iot_provisioning_client_get_user_name");
  int rc = az_iot_provisioning_client_get_user_name(
    &provisioning,
    mqtt_client_username_buffer, 
    sizeof(mqtt_client_username_buffer),
    NULL
  );
  if (az_result_failed(rc))
  {
    char out[40];
    sprintf(out, "0x%08x", rc);
    Serial.println(out);
    return;
  }
  Logger.Info("az_iot_provisioning_client_get_client_id");
  if (az_result_failed(az_iot_provisioning_client_get_client_id(
    &provisioning,
    mqtt_client_id_buffer,
    sizeof(mqtt_client_id_buffer),
    NULL
  ))){
    Logger.Error("Failed get_client_id");
    return;
  }
  Logger.Info(mqtt_client_id_buffer);
  Logger.Info(mqtt_client_username_buffer);
  esp_mqtt_client_config_t mqtt_config_DPS;
  memset(&mqtt_config_DPS, 0, sizeof(mqtt_config_DPS));
  mqtt_config_DPS.uri = hostDPS;
  mqtt_config_DPS.port = mqtt_port;
  mqtt_config_DPS.client_id = mqtt_client_id_buffer;
  mqtt_config_DPS.username = mqtt_client_username_buffer;
  mqtt_config_DPS.protocol_ver = MQTT_PROTOCOL_V_3_1_1;
#ifdef IOT_CONFIG_USE_X509_CERT
  Logger.Info("MQTT client using X509 Certificate authentication");
  mqtt_config_DPS.client_cert_pem = IOT_CONFIG_DEVICE_CERT;
  mqtt_config_DPS.client_key_pem = IOT_CONFIG_DEVICE_CERT_PRIVATE_KEY;
#else // Using SAS key
  mqtt_config_DPS.password = (const char*)az_span_ptr(sasToken.Get());
#endif

  mqtt_config_DPS.keepalive = 30;
  mqtt_config_DPS.disable_clean_session = true;
  mqtt_config_DPS.disable_auto_reconnect = false;
  mqtt_config_DPS.event_handle = mqtt_dps_event_handler;
  mqtt_config_DPS.user_context = NULL;
  mqtt_config_DPS.cert_pem = (const char*)ca_pem;

  mqtt_client_DPS = esp_mqtt_client_init(&mqtt_config_DPS);

  if (mqtt_client_DPS == NULL)
  {
    Logger.Error("Failed creating mqtt DPS client");
    return;
  }

  esp_err_t start_result = esp_mqtt_client_start(mqtt_client_DPS);

  if (start_result != ESP_OK)
  {
    Logger.Error("Could not start mqtt DPS client; error code:" + start_result);
    return;
  }
  else
  {
    Logger.Info("MQTT DPS client started");
  }

  Logger.Info("DPS MQTT REGISTRY DEVICE");
  dps_payload = "{ \"registrationId\": \"" + String(device_id) + "\" }";
  dps_topic  = "$dps/registrations/PUT/iotdps-register/?$rid=1";
  if (esp_mqtt_client_publish(
          mqtt_client_DPS,
          (const char*)dps_topic.c_str(),
          (const char*)dps_payload.c_str(),
          dps_payload.length(),
          MQTT_QOS1,
          DO_NOT_RETAIN_MSG)
      == 0)
  {
    Logger.Error("Failed publishing");
  }
  else
  {
    Logger.Info("DPS Message published successfully");
  }

  /*AGUARDANDO FINALIZACAO DO REGISTRO */
  delay(1000);

  Logger.Info("DPS MQTT REGISTRY STATUS");
  Logger.Info(operationId);
  dps_payload = "{}";
  dps_topic  = "$dps/registrations/GET/iotdps-get-operationstatus/?$rid=1&operationId=" + String(operationId);
  Logger.Info(dps_topic);
  if (esp_mqtt_client_publish(
          mqtt_client_DPS,
          (const char*)dps_topic.c_str(),
          (const char*)dps_payload.c_str(),
          dps_payload.length(),
          MQTT_QOS1,
          DO_NOT_RETAIN_MSG)
      == 0)
  {
    Logger.Error("Failed publishing");
  }
  else
  {
    Logger.Info("DPS Message published successfully");
  }
  
  delay(1000);
  
  az_iot_hub_client_options options = az_iot_hub_client_options_default();
  options.user_agent = AZ_SPAN_FROM_STR(AZURE_SDK_CLIENT_USER_AGENT);

  if (az_result_failed(az_iot_hub_client_init(
          &client,
          az_span_create((uint8_t*)host, strlen(host)),
          az_span_create((uint8_t*)device_id, strlen(device_id)),
          &options)))
  {
    Logger.Error("Failed initializing Azure IoT Hub client");
    return;
  }

  size_t client_id_length;
  if (az_result_failed(az_iot_hub_client_get_client_id(
          &client, mqtt_client_id, sizeof(mqtt_client_id) - 1, &client_id_length)))
  {
    Logger.Error("Failed getting client id");
    return;
  }

  if (az_result_failed(az_iot_hub_client_get_user_name(
          &client, mqtt_username, sizeofarray(mqtt_username), NULL)))
  {
    Logger.Error("Failed to get MQTT clientId, return code");
    return;
  }

  Logger.Info("Client ID: " + String(mqtt_client_id));
  Logger.Info("Username: " + String(mqtt_username));

  (void)initializeMqttClient();
  
}

static int initializeMqttClient()
{
#ifndef IOT_CONFIG_USE_X509_CERT
  if (sasToken.Generate(SAS_TOKEN_DURATION_IN_MINUTES) != 0)
  {
    Logger.Error("Failed generating SAS token");
    return 1;
  }
#endif

  String assignedHub = String(host);
  assignedHub = "mqtts://" + assignedHub;

  esp_mqtt_client_config_t mqtt_config;
  memset(&mqtt_config, 0, sizeof(mqtt_config));
  mqtt_config.uri = assignedHub.c_str();
  mqtt_config.port = mqtt_port;
  mqtt_config.client_id = mqtt_client_id;
  mqtt_config.username = mqtt_username;

#ifdef IOT_CONFIG_USE_X509_CERT
  Logger.Info("MQTT client using X509 Certificate authentication");
  mqtt_config.client_cert_pem = IOT_CONFIG_DEVICE_CERT;
  mqtt_config.client_key_pem = IOT_CONFIG_DEVICE_CERT_PRIVATE_KEY;
#else // Using SAS key
  mqtt_config.password = (const char*)az_span_ptr(sasToken.Get());
#endif

  mqtt_config.keepalive = 30;
  mqtt_config.disable_clean_session = 0;
  mqtt_config.disable_auto_reconnect = false;
  mqtt_config.event_handle = mqtt_event_handler;
  mqtt_config.user_context = NULL;
  mqtt_config.cert_pem = (const char*)ca_pem;

  mqtt_client = esp_mqtt_client_init(&mqtt_config);

  if (mqtt_client == NULL)
  {
    Logger.Error("Failed creating mqtt client");
    return 1;
  }

  esp_err_t start_result = esp_mqtt_client_start(mqtt_client);

  if (start_result != ESP_OK)
  {
    Logger.Error("Could not start mqtt client; error code:" + start_result);
    return 1;
  }
  else
  {
    Logger.Info("MQTT client started");
    return 0;
  }
}

/*
 * @brief           Gets the number of seconds since UNIX epoch until now.
 * @return uint32_t Number of seconds.
 */
static uint32_t getEpochTimeInSecs() { return (uint32_t)time(NULL); }

static void establishConnection()
{
  connectToWiFi();
  initializeTime();
  initializeIoTHubClient();
  //(void)initializeMqttClient();
}

static void generateTelemetryPayload()
{
  // You can generate the JSON using any lib you want. Here we're showing how to do it manually, for simplicity.
  // This sample shows how to generate the payload using a syntax closer to regular delevelopment for Arduino, with
  // String type instead of az_span as it might be done in other samples. Using az_span has the advantage of reusing the 
  // same char buffer instead of dynamically allocating memory each time, as it is done by using the String type below.
  telemetry_payload = "{ \"msgCount\": " + String(telemetry_send_count++) + " }";
}

static void sendTelemetry()
{
  Logger.Info("Sending telemetry ...");

  // The topic could be obtained just once during setup,
  // however if properties are used the topic need to be generated again to reflect the
  // current values of the properties.
  if (az_result_failed(az_iot_hub_client_telemetry_get_publish_topic(
          &client, NULL, telemetry_topic, sizeof(telemetry_topic), NULL)))
  {
    Logger.Error("Failed az_iot_hub_client_telemetry_get_publish_topic");
    return;
  }

  generateTelemetryPayload();

  if (esp_mqtt_client_publish(
          mqtt_client,
          telemetry_topic,
          (const char*)telemetry_payload.c_str(),
          telemetry_payload.length(),
          MQTT_QOS1,
          DO_NOT_RETAIN_MSG)
      == 0)
  {
    Logger.Error("Failed publishing");
  }
  else
  {
    Logger.Info("Message published successfully");
  }
}

// Arduino setup and loop main functions.

void setup() { 
  esp_log_level_set("*", ESP_LOG_VERBOSE);

  establishConnection(); 
}

void loop()
{
  if (WiFi.status() != WL_CONNECTED)
  {
    connectToWiFi();
  }
#ifndef IOT_CONFIG_USE_X509_CERT
  else if (sasToken.IsExpired())
  {
    Logger.Info("SAS token expired; reconnecting with a new one.");
    //(void)esp_mqtt_client_destroy(mqtt_client);
    //initializeMqttClient();
  }
#endif
  else if (millis() > next_telemetry_send_time_ms)
  {
    sendTelemetry();
    next_telemetry_send_time_ms = millis() + TELEMETRY_FREQUENCY_MILLISECS;
  }
}
