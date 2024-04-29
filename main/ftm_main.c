

#include "esp_event.h"
#include "esp_mac.h"
#include <errno.h>
#include <inttypes.h>
#include "cmd_system.h"
#include "argtable3/argtable3.h"
#include "esp_console.h"
#include "esp_err.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>
#include "nvs_flash.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "sdkconfig.h"
#include <sys/time.h>
#include <time.h>
#include "driver/temperature_sensor.h"

wifi_ftm_initiator_cfg_t ftmi_cfg = {
    .frm_count = 24,   // RTT burst size (8, 4의 제곱수)
    .burst_period = 0, // (0 - No pref)
};

// AP SSID
const char *SSID = "ESP32_AP_20";

// ftm 주기 (1000 / portTICK_PERIOD_MS = 1초)
static const TickType_t xPeriod = 1000 / portTICK_PERIOD_MS;

static wifi_ftm_report_entry_t *s_ftm_report;
static uint8_t s_ftm_report_num_entries;
static uint32_t s_rtt_est, s_dist_est, s_rtt_raw;
static EventGroupHandle_t s_ftm_event_group;
static const char *TAG_STA = "ftm_station";
EventBits_t bits;

static const int FTM_REPORT_BIT = BIT0;
static const int FTM_FAILURE_BIT = BIT1;

uint16_t g_scan_ap_num;
wifi_ap_record_t *g_ap_list_buffer;

struct timeval tv_now;
TickType_t xLastWakeTime;

temperature_sensor_handle_t temp_sensor = NULL;
temperature_sensor_config_t temp_sensor_config = TEMPERATURE_SENSOR_CONFIG_DEFAULT(10, 50);
float tsens_out;

wifi_ap_record_t *find_ftm_responder_ap()
{
    wifi_scan_config_t scan_config = {0};
    scan_config.ssid = (uint8_t *)SSID;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    esp_wifi_scan_start(&scan_config, true);
    esp_wifi_scan_get_ap_num(&g_scan_ap_num);

    g_ap_list_buffer = malloc(g_scan_ap_num * sizeof(wifi_ap_record_t));
    esp_wifi_scan_get_ap_records(&g_scan_ap_num, (wifi_ap_record_t *)g_ap_list_buffer);
    for (int i = 0; i < g_scan_ap_num; i++)
    {
        if (strcmp((const char *)g_ap_list_buffer[i].ssid, SSID) == 0)
        {
            return &g_ap_list_buffer[i];
        }
    }
    free(g_ap_list_buffer);
    return NULL;
}

static void event_handler(void *arg, esp_event_base_t event_base,
                          int32_t event_id, void *event_data)
{
    if (event_id == WIFI_EVENT_FTM_REPORT)
    {
        wifi_event_ftm_report_t *event = (wifi_event_ftm_report_t *)event_data;

        s_rtt_est = event->rtt_est;
        s_dist_est = event->dist_est;
        s_ftm_report = event->ftm_report_data;
        s_ftm_report_num_entries = event->ftm_report_num_entries;
        s_rtt_raw = event->rtt_raw;
        if (event->status == FTM_STATUS_SUCCESS)
        {
            xEventGroupSetBits(s_ftm_event_group, FTM_REPORT_BIT);
        }
        else
        {
            /*ESP_LOGI(TAG_STA, "FTM procedure with Peer(" MACSTR ") failed! (Status - %d)",
                     MAC2STR(event->peer_mac), event->status);*/
            xEventGroupSetBits(s_ftm_event_group, FTM_FAILURE_BIT);
        }
    }
}

static void ftm_process_report(int logtimer)
{

    // Get converted sensor data
    
    

    printf("epoch = %d, Distance = %" PRId32 ".%02" PRId32 " m\n", logtimer, s_dist_est / 100, s_dist_est % 100);

    int i;

    if (s_ftm_report_num_entries == 0)
        return;

    for (i = 0; i < s_ftm_report_num_entries; i++)
    {
        // 온도 센서 데이터
        ESP_ERROR_CHECK(temperature_sensor_get_celsius(temp_sensor, &tsens_out));
        
        // Timestamp (기기 부팅 후 시간)
        gettimeofday(&tv_now, NULL);
        int64_t time_us = (int64_t)tv_now.tv_sec * 1000000L + (int64_t)tv_now.tv_usec; // µs

        printf("%d,%lld,%6d,%7" PRIi32 ",%14llu,%14llu,%14llu,%14llu,%6d,%.02f\n",
               logtimer, time_us, s_ftm_report[i].dlog_token, s_ftm_report[i].rtt, s_ftm_report[i].t1, s_ftm_report[i].t2, s_ftm_report[i].t3, s_ftm_report[i].t4, s_ftm_report[i].rssi, tsens_out);
    }
}

void initialize_wifi(void)
{
    esp_log_level_set("wifi", ESP_LOG_NONE);

    ESP_ERROR_CHECK(esp_netif_init());
    s_ftm_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    ESP_ERROR_CHECK(
        esp_event_handler_instance_register(
            WIFI_EVENT,
            ESP_EVENT_ANY_ID,
            &event_handler,
            NULL,
            &instance_any_id));

    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_NULL));
    ESP_ERROR_CHECK(esp_wifi_start());
}

void app_main(void)
{

    ESP_ERROR_CHECK(temperature_sensor_install(&temp_sensor_config, &temp_sensor));
    ESP_ERROR_CHECK(temperature_sensor_enable(temp_sensor));

    int logtimer = 0;

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    initialize_wifi();

    wifi_ap_record_t *ap_record = find_ftm_responder_ap();
    if (ap_record == NULL)
    {
        ESP_LOGI(TAG_STA, "No FTM Responder with the SSID: %s",
                 SSID);
        return;
    }

    memcpy(ftmi_cfg.resp_mac, ap_record->bssid, 6);
    ftmi_cfg.channel = ap_record->primary;

    /*ESP_LOGI(TAG_STA, "|Requesting FTM session with Frm Count - |%d| Burst Period - |%d|mSec (0: No Preference)",
             ftmi_cfg.frm_count, ftmi_cfg.burst_period * 100);*/

    printf("FTM START\n");

    while (true)
    {
        xLastWakeTime = xTaskGetTickCount();
        logtimer++;

        esp_wifi_ftm_initiate_session(&ftmi_cfg);

        bits = xEventGroupWaitBits(s_ftm_event_group, FTM_REPORT_BIT | FTM_FAILURE_BIT, pdTRUE, pdFALSE, portMAX_DELAY);

        if (bits & FTM_REPORT_BIT)
        {
            ftm_process_report(logtimer);
            free(s_ftm_report);
            s_ftm_report = NULL;
            /*ESP_LOGI(TAG_STA, "|est_RTT: |%" PRId32 "| est_dist: |%" PRId32 ".%02" PRId32 "| est_report_num: |%" PRId8 "| est_raw: |%" PRId32 "|",
                     s_rtt_est, s_dist_est / 100, s_dist_est % 100, s_ftm_report_num_entries, s_rtt_raw); */
        }
        else
        {
            xEventGroupClearBits(s_ftm_event_group, FTM_FAILURE_BIT);
            // ESP_LOGI(TAG_STA, "FTM Failed");
            printf("%d,FTM Failed\n", logtimer);
        }

        esp_wifi_ftm_end_session();

        /* 로그 기록 시간
        logtimer -> 초단위 */
        if (logtimer >= 300)
        {
            printf("FTM STOP\n");
            break;
        }
        else
        {
            vTaskDelayUntil(&xLastWakeTime, xPeriod);
            continue;
        }
    }
}