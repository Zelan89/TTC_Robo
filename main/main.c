/* TTC_Robo */
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "SoftAP.h"
#include "tcp_server.h"
#include "websocket_server.h"
#include "Servo.h"

const char *TAG = "TTC_Robo";

void app_main()
{
	ESP_LOGI(TAG, "Hello from %s!", TAG);
	
	wifi_init();
	servo_init();
	websocket_server_init();
}