/*
 * OFF-GRID Posture Sender (ESP32-C3 SuperMini)
 * Fix: Sends "Keep-Alive" packets during calibration to prevent disconnects.
 */
#include <stdio.h>
#include <math.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_now.h"
#include "nvs_flash.h"

// --- CONFIGURATION ---
#define ESP_NOW_CHANNEL    1 
uint8_t BROADCAST_MAC[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// --- PINS ---
#define I2C_SDA_IO         6
#define I2C_SCL_IO         7
#define LED_PIN            8
#define BUTTON_PIN         9 
#define VIB_MOTOR_PIN      3   

#define BAD_POSTURE_ANGLE  15.0f 

// --- PACKETS ---
typedef struct {
    float pitch;
    float roll;
    int battery_level;
} posture_packet_t;

typedef struct {
    uint8_t command_id; 
    uint8_t value;      
} command_packet_t;

static const char *TAG = "SENDER";
static float offset_pitch = 0;
static float offset_roll = 0;
static bool trigger_calibration = false;
static bool vibration_enabled = true; 

// --- I2C / MPU6050 ---
#define MPU6050_ADDR       0x68
#define RAD_TO_DEG         57.2957795131

static void i2c_init(void) {
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = I2C_SDA_IO,
        .scl_io_num = I2C_SCL_IO,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 400000,
    };
    i2c_param_config(0, &conf);
    i2c_driver_install(0, conf.mode, 0, 0, 0);
}

static void mpu_wake(void) {
    uint8_t data[2] = {0x6B, 0x00}; 
    i2c_master_write_to_device(0, MPU6050_ADDR, data, 2, 100);
}

static void read_mpu_data(float *p, float *r) {
    uint8_t data[6];
    uint8_t reg = 0x3B; 
    if (i2c_master_write_read_device(0, MPU6050_ADDR, &reg, 1, data, 6, 100) == ESP_OK) {
        int16_t ax = (int16_t)((data[0] << 8) | data[1]);
        int16_t ay = (int16_t)((data[2] << 8) | data[3]);
        int16_t az = (int16_t)((data[4] << 8) | data[5]);
        float x = ax / 16384.0;
        float y = ay / 16384.0;
        float z = az / 16384.0;
        *p = atan2(-x, sqrt(y*y + z*z)) * RAD_TO_DEG;
        *r = atan2(y, z) * RAD_TO_DEG;
    }
}

// --- ESP-NOW CALLBACK ---
static void on_recv(const esp_now_recv_info_t * info, const uint8_t * data, int len) {
    if (len == sizeof(command_packet_t)) {
        command_packet_t *cmd = (command_packet_t *)data;
        if (cmd->command_id == 1) {
            trigger_calibration = true; 
        }
        else if (cmd->command_id == 2) {
            vibration_enabled = (cmd->value == 1);
            gpio_set_level(LED_PIN, 0); vTaskDelay(50/portTICK_PERIOD_MS);
            gpio_set_level(LED_PIN, 1);
        }
    }
}

static void wifi_init_offline(void) {
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_channel(ESP_NOW_CHANNEL, WIFI_SECOND_CHAN_NONE));
}

static void init_esp_now(void) {
    ESP_ERROR_CHECK(esp_now_init());
    ESP_ERROR_CHECK(esp_now_register_recv_cb(on_recv));

    esp_now_peer_info_t peerInfo = {};
    memcpy(peerInfo.peer_addr, BROADCAST_MAC, 6);
    peerInfo.channel = ESP_NOW_CHANNEL;
    peerInfo.encrypt = false;
    esp_now_add_peer(&peerInfo);
}

void app_main(void) {
    nvs_flash_init();
    
    gpio_reset_pin(LED_PIN); gpio_set_direction(LED_PIN, GPIO_MODE_OUTPUT);
    gpio_reset_pin(VIB_MOTOR_PIN); gpio_set_direction(VIB_MOTOR_PIN, GPIO_MODE_OUTPUT); 
    gpio_reset_pin(BUTTON_PIN); gpio_set_direction(BUTTON_PIN, GPIO_MODE_INPUT);
    gpio_set_pull_mode(BUTTON_PIN, GPIO_PULLUP_ONLY);

    i2c_init();
    mpu_wake();
    wifi_init_offline();
    init_esp_now();

    ESP_LOGI(TAG, "Sender Ready.");
    posture_packet_t packet;
    
    // Initialize packet with safe defaults
    packet.pitch = 0; packet.roll = 0; packet.battery_level = 95;

    while (1) {
        float raw_p, raw_r;
        read_mpu_data(&raw_p, &raw_r);

        // --- CALIBRATION ---
        if (trigger_calibration || gpio_get_level(BUTTON_PIN) == 0) {
            if (!trigger_calibration) vTaskDelay(50/portTICK_PERIOD_MS); 

            if (trigger_calibration || gpio_get_level(BUTTON_PIN) == 0) {
                gpio_set_level(VIB_MOTOR_PIN, 0); 
                
                // 3-SECOND COUNTDOWN (With Keep-Alive)
                for(int i=3; i>0; i--) {
                    // FIX: Send data during wait so receiver doesn't disconnect!
                    esp_now_send(BROADCAST_MAC, (uint8_t *) &packet, sizeof(packet));
                    
                    gpio_set_level(LED_PIN, 0); vTaskDelay(200 / portTICK_PERIOD_MS);
                    gpio_set_level(LED_PIN, 1); vTaskDelay(800 / portTICK_PERIOD_MS);
                }

                read_mpu_data(&raw_p, &raw_r);
                offset_pitch = raw_p;
                offset_roll = raw_r;
                trigger_calibration = false; 

                // Final confirmation
                for(int i=0; i<3; i++) {
                    gpio_set_level(LED_PIN, 0); vTaskDelay(100/portTICK_PERIOD_MS);
                    gpio_set_level(LED_PIN, 1); vTaskDelay(100/portTICK_PERIOD_MS);
                }
            }
        }

        // --- DATA ---
        float real_pitch = raw_p - offset_pitch;
        float real_roll  = raw_r - offset_roll;

        // --- FEEDBACK ---
        if (fabs(real_pitch) > BAD_POSTURE_ANGLE) {
            if (vibration_enabled) {
                gpio_set_level(VIB_MOTOR_PIN, 1);
            }
            gpio_set_level(LED_PIN, 0); 
            
            packet.pitch = real_pitch;
            packet.roll  = real_roll;
            packet.battery_level = 95; 
            esp_now_send(BROADCAST_MAC, (uint8_t *) &packet, sizeof(packet));

            vTaskDelay(200 / portTICK_PERIOD_MS); // Blind time

            gpio_set_level(VIB_MOTOR_PIN, 0);
            gpio_set_level(LED_PIN, 1); 
            vTaskDelay(50 / portTICK_PERIOD_MS); 
        } 
        else {
            gpio_set_level(VIB_MOTOR_PIN, 0);
            gpio_set_level(LED_PIN, 1); 

            packet.pitch = real_pitch;
            packet.roll  = real_roll;
            packet.battery_level = 95; 
            esp_now_send(BROADCAST_MAC, (uint8_t *) &packet, sizeof(packet));
            
            vTaskDelay(100 / portTICK_PERIOD_MS); 
        }
    }
}