/* BSD Socket API Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <string.h>
#include <sys/param.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event_loop.h"
#include "esp_log.h"

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include <lwip/netdb.h>

#include "tcp_server.h"
/* The examples use simple WiFi configuration that you can set via
   'make menuconfig'.
   If you'd rather not, just change the below entries to strings with
   the config you want - ie #define EXAMPLE_WIFI_SSID "mywifissid"
*/

#define PORT CONFIG_SERVER_PORT

const int IPV4_GOTIP_BIT = BIT0;
#ifdef CONFIG_SERVER_IPV6
const int IPV6_GOTIP_BIT = BIT1;
#endif

static const char *TAG = "tcp_server";

static void tcp_server_task(void *pvParameters)
{
	char rx_buffer[128];
	char addr_str[128];
	int addr_family;
	int ip_protocol;

#ifdef CONFIG_IPV4
	struct sockaddr_in destAddr;
	destAddr.sin_addr.s_addr = htonl(INADDR_ANY);
	destAddr.sin_family = AF_INET;
	destAddr.sin_port = htons(PORT);
	addr_family = AF_INET;
	ip_protocol = IPPROTO_IP;
	inet_ntoa_r(destAddr.sin_addr, addr_str, sizeof(addr_str) - 1);
#else // IPV6
	struct sockaddr_in6 destAddr;
	bzero(&destAddr.sin6_addr.un, sizeof(destAddr.sin6_addr.un));
	destAddr.sin6_family = AF_INET6;
	destAddr.sin6_port = htons(PORT);
	addr_family = AF_INET6;
	ip_protocol = IPPROTO_IPV6;
	inet6_ntoa_r(destAddr.sin6_addr, addr_str, sizeof(addr_str) - 1);
#endif	

	int listen_sock = socket(addr_family, SOCK_STREAM, ip_protocol);
	if (listen_sock < 0) {
		ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
		//break;
	}
	ESP_LOGI(TAG, "Socket created");

	int err = bind(listen_sock, (struct sockaddr *)&destAddr, sizeof(destAddr));
	if (err != 0) {
		ESP_LOGE(TAG, "Socket unable to bind: errno %d", errno);
		//break;
	}
	ESP_LOGI(TAG, "Socket binded to port %d", PORT);
	
	while (1) {
		err = listen(listen_sock, 1);
		if (err != 0) {
			ESP_LOGE(TAG, "Error occured during listen: errno %d", errno);
			break;
		}
		ESP_LOGI(TAG, "Socket listening to port %d", PORT);

#ifdef CONFIG_IPV6
		struct sockaddr_in6 sourceAddr; // Large enough for both IPv4 or IPv6
#else
		        struct sockaddr_in sourceAddr;
#endif
		uint addrLen = sizeof(sourceAddr);
		int sock = accept(listen_sock, (struct sockaddr *)&sourceAddr, &addrLen);
		if (sock < 0) {
			ESP_LOGE(TAG, "Unable to accept connection: errno %d", errno);
			break;
		}
		ESP_LOGI(TAG, "Socket accepted");

		while (1) {
			int len = recv(sock, rx_buffer, sizeof(rx_buffer) - 1, 0);
			// Error occured during receiving
			if(len < 0) {
				ESP_LOGE(TAG, "recv failed: errno %d", errno);
				break;
			}
			// Connection closed
			else if(len == 0) {
				ESP_LOGI(TAG, "Connection closed");
				break;
			}
			// Data received
			else {
#ifdef CONFIG_IPV6
				// Get the sender's ip address as string
				if(sourceAddr.sin6_family == PF_INET) {
					inet_ntoa_r(((struct sockaddr_in *)&sourceAddr)->sin_addr.s_addr, addr_str, sizeof(addr_str) - 1);
				} else if(sourceAddr.sin6_family == PF_INET6) {
					inet6_ntoa_r(sourceAddr.sin6_addr, addr_str, sizeof(addr_str) - 1);
				}
#else
				inet_ntoa_r(((struct sockaddr_in *)&sourceAddr)->sin_addr.s_addr, addr_str, sizeof(addr_str) - 1);
#endif

				rx_buffer[len] = 0;  // Null-terminate whatever we received and treat like a string
				ESP_LOGI(TAG, "Received %d bytes from %s:", len, addr_str);
				ESP_LOGI(TAG, "%s", rx_buffer);

				int err = send(sock, rx_buffer, len, 0);
				if (err < 0) {
					ESP_LOGE(TAG, "Error occured during sending: errno %d", errno);
					break;
				}
			}
		}

		if (sock != -1) {
			ESP_LOGE(TAG, "Shutting down socket and restarting...");
			shutdown(sock, 0);
			close(sock);
		}
	}
	vTaskDelete(NULL);
}


void tcp_server_init()
{
	xTaskCreate(tcp_server_task, "tcp_server", 4096, NULL, 5, NULL);
}