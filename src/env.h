#pragma once

/**
 * @file env.h
 * @brief Global Environment Configuration
 * 
 * This file serves as the single source of truth for deployment-specific 
 * parameters. Modify these values before compiling for a new environment
 * (e.g., migrating from development/staging to production server).
 */

/* -------------------------------------------------------------------------- */
/*                              MQTT Configuration                            */
/* -------------------------------------------------------------------------- */

#define MQTT_BROKER_DEFAULT     "192.168.1.11"
#define MQTT_PORT_DEFAULT       1883
#define MQTT_USER               "drainage01"
#define MQTT_PASS               "password123"
#define MQTT_QOS                1


/* -------------------------------------------------------------------------- */
/*                            HTTP API & Endpoints                            */
/* -------------------------------------------------------------------------- */

// Default host used if the device is not provisioned via Captive Portal
#define DEFAULT_BACKEND_HOST    "192.168.1.100"
constexpr int DEFAULT_BACKEND_PORT = 3000;

// REST API endpoint routes
#define API_ENDPOINT_CONFIG     "/api/devices/%s/config"
#define API_ENDPOINT_UPLOAD     "/api/upload-image"
#define API_ENDPOINT_SNAPSHOT   "/api/devices/%s/snapshot"


/* -------------------------------------------------------------------------- */
/*                              NTP (Time Sync)                               */
/* -------------------------------------------------------------------------- */

#define NTP_SERVER              "pool.ntp.org"
