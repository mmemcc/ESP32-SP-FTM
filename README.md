**`ftm_sp/main/ftm_main.c`**

- `줄 번호 22-31`

```c
// FTM burst 설정
wifi_ftm_initiator_cfg_t ftmi_cfg = {
    .frm_count = 8,   // RTT burst size (4, 8의 배수)
    .burst_period = 0, // (0 - No pref)
};

// AP SSID
const char *SSID = "AP_SSID";

// ftm 주기 (1000 / portTICK_PERIOD_MS = 1초)
static const TickType_t xPeriod = 1000 / portTICK_PERIOD_MS;
```

// FTM burst 설정

`frm_count` : burst size (4, 8의 배수)

`burst_period` : burst 주기 (100 ms 단위), 0 으로 설정 시 자동

// AP SSID 

`SSID` : 연결할 AP의 SSID

// ftm 주기

`xPeriod` : ftm 주기. 1000 / portTICK_PERIOD_MS = 1sec

- `줄 번호 195-end`

```c
// 기록 시간
if (logtimer >= 300)
{
printf("FTM STOP\n");
break;
}
else
{
vTaskDelayUntil(&xLastWakeTime, xPeriod);
continue;
```

`if (logtimer >= 300)` : 기록 시간 [sec]