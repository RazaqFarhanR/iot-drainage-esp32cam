#pragma once

/* -------------------------------------------------------------------------- */
/*                              MQTT Configuration                            */
/* -------------------------------------------------------------------------- */

#ifdef ENV_PROD
    #define MQTT_BROKER_DEFAULT     "broker.emqx.io"
    #define MQTT_PORT_DEFAULT       1883
    #define MQTT_USER               "drainage01"
    #define MQTT_PASS               "password123"
#else
    #define MQTT_BROKER_DEFAULT     "192.168.1.11"
    #define MQTT_PORT_DEFAULT       1883
    #define MQTT_USER               "drainage01"
    #define MQTT_PASS               "password123"
#endif

#define MQTT_QOS                1
#define MQTT_TOPIC_TELEMETRY    "compro9.26.telyu-iot-drainage-be/sensor-data"
#define MQTT_TOPIC_LOG          "compro9.26.telyu-iot-drainage-be/sensor-log"
#define MQTT_TOPIC_DEVICE_INFO  "compro9.26.telyu-iot-drainage-be/device-info"

/* -------------------------------------------------------------------------- */
/*                            HTTP API & Endpoints                            */
/* -------------------------------------------------------------------------- */

#ifdef ENV_PROD
    #define DEFAULT_BACKEND_HOST    "api.production.com" // Update to actual production API host if needed
    constexpr int DEFAULT_BACKEND_PORT = 3000;
#else
    #define DEFAULT_BACKEND_HOST    "192.168.1.100"
    constexpr int DEFAULT_BACKEND_PORT = 3000;
#endif

// REST API endpoint routes
#define API_ENDPOINT_CONFIG     "/api/devices/%s/config"
#define API_ENDPOINT_UPLOAD     "/api/image"
#define API_ENDPOINT_SNAPSHOT   "/api/devices/%s/snapshot"

/* -------------------------------------------------------------------------- */
/*                              NTP (Time Sync)                               */
/* -------------------------------------------------------------------------- */

#define NTP_SERVER              "pool.ntp.org"
