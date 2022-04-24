/* Websocket server
*/

#include "esp_log.h"
#include "esp_heap_caps.h"
#include "mbedtls/base64.h"
#include "mbedtls/sha1.h"
#include "lwip/sockets.h"

#include "cJSON.h"
#include <string.h>
#include "websocket_server.h"
#include "Servo.h"

#define PORT CONFIG_SERVER_PORT
#define TASK_NORMAL_PRIORITY  (configMAX_PRIORITIES / 2)

#define WS_PORT				8080	/**< \brief TCP Port for the Server*/
#define WS_CLIENT_KEY_L		24		/**< \brief Length of the Client Key*/
#define SHA1_RES_L			20		/**< \brief SHA1 result*/
#define WS_STD_LEN			125		/**< \brief Maximum Length of standard length frames*/
#define WS_SPRINTF_ARG_L	4		/**< \brief Length of sprintf argument for string (%.*s)*/
#define WS_PACKET_LENGTH	2048
#define WS_HEADER_LENGTH	2
#define WS_MASK_LENGTH		4

const char WS_sec_WS_keys[] = "Sec-WebSocket-Key:";
const char WS_sec_conKey[] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
const char WS_srv_hs[] = "HTTP/1.1 101 Switching Protocols \r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: %.*s\r\n\r\n";

/* USER CODE BEGIN PV */
/* Private variables ---------------------------------------------------------*/
static const char *TAG = "websocket_server";
char guid_str[] = { "258EAFA5-E914-47DA-95CA-C5AB0DC85B11" };
unsigned char hash[20] = { 0 };
char str_buf[1000] = { '\0' };
char str_key[512] = { };
char str_out[32] = { };
char str_buf2[256] = { };
extern QueueHandle_t servoFeederQueue;
/* USER CODE END PV */

/* Read functions*/
void read_ws_text(int conn, char* data, uint64_t length)
{
	ESP_LOGI(TAG, "Received TXT");
	data[length] = '\0';

	ESP_LOGI(TAG, "%s", data);
	cJSON *root = cJSON_Parse(data);
	
	joystick ballSpin = { 0 };
	coordinates boxPosition = { 0 };
	
	if (cJSON_HasObjectItem(root, "BPM") == true)
	{
		uint32_t BPM = cJSON_GetObjectItem(root, "BPM")->valueint;
		xQueueSend(servoFeederQueue, &BPM, 0);
	}
	
	if (cJSON_HasObjectItem(root, "angle") == true)
	{
		ballSpin.angle = cJSON_GetObjectItem(root, "angle")->valuedouble;
		ballSpin.distance = cJSON_GetObjectItem(root, "distance")->valuedouble;
		//xQueueSend(servoFeederQueue, &ballSpin, 0);
	}
	
	if (cJSON_HasObjectItem(root, "x") == true)
	{
		boxPosition.x = cJSON_GetObjectItem(root, "x")->valuedouble;
		boxPosition.y = cJSON_GetObjectItem(root, "y")->valuedouble;
		//xQueueSend(servoFeederQueue, &ballSpin, 0);
	}
	
	cJSON_Delete(root);
}

void read_ws_binary(int conn, uint8_t* data, uint64_t length)
{
	ESP_LOGI(TAG, "Received BIN");
	int i;
	for (i = 0; i < length; i++)
	{
		if (i > 0) printf(":");
		printf("%02X", data[i]);
	}
	printf("\n");
}

void read_ws_ping(int conn, uint8_t* data, uint64_t length)
{
	ESP_LOGI(TAG, "Received PING");
	websocket_write(conn, WS_OP_PON, (char*)data, length);
}

void read_ws_pong(int conn, uint8_t* data, uint64_t length)
{
	ESP_LOGI(TAG, "Received PONG");
}

void read_ws_close(int conn, uint8_t* data, uint64_t length)
{
	ESP_LOGI(TAG, "Received CLOSE");
}

/*Write websocket message function*/
err_t websocket_write(int conn, WS_OPCODES opcode, char* p_data, size_t length) 
{
	//check if we have an open connection
	if(conn < 0)
		return ERR_CONN;

	//currently only frames with a payload length <WS_STD_LEN are supported
	if(length > WS_STD_LEN)
		return ERR_VAL;

	//write result buffer
	err_t result;

	//prepare header
	WS_frame_header_t hdr;
	hdr.FIN = true;
	hdr.payload_code = length;
	hdr.mask = false;
	hdr.reserved = 0;
	hdr.opcode = opcode;

	//send header
	result = send(conn, &hdr, WS_HEADER_LENGTH, 0);
	
	//check if header was send
	if(result == ESP_FAIL)
		return result;
	
	//send payload
	result = send(conn, p_data, length, 0);
	
	//check if payload was send
	if (result == ESP_FAIL)
		return result;
	
	return result;
}

static void  client_connection(void *argument)
{
	char data[WS_PACKET_LENGTH] = {};
	int ret_r, accept_sock;

	WS_frame_full_t frame_full;
	accept_sock = *(int*)argument;

	for (;;)
	{
		ret_r = recv(accept_sock, data, WS_HEADER_LENGTH, 0);

		if (ret_r < 0) 
		{
			ESP_LOGE(TAG, "recv failed: errno %d", errno);
			break;
		}
		
		// Connection closed
		else if(ret_r == 0) 
		{
			ESP_LOGI(TAG, "Connection closed");
			close(accept_sock);
			vTaskDelete(NULL);
			break;
		}
		
		// Data received
		if (ret_r > 0) 
		{	
			frame_full.frame_header = *(WS_frame_header_t*)&data;	
			ESP_LOGI(TAG, "Websocket opcode = %d", frame_full.frame_header.opcode);
			
			if (frame_full.frame_header.payload_code <= 125)
			{				
				frame_full.payload_length = frame_full.frame_header.payload_code;
			}
			else if (frame_full.frame_header.payload_code == 126)
			{
				if (recv(accept_sock, data, 2, 0) <= 0)
				{					
					ESP_LOGW(TAG, "Failed to receive 2 bytes length");
				}
				frame_full.payload_length = ((uint32_t)(data[0] << 8) | (data[1]));	
			}
			else if (frame_full.frame_header.payload_code == 127)
			{
				if (recv(accept_sock, data, 8, 0) <= 0)
				{					
					ESP_LOGW(TAG, "Failed to receive 8 bytes length");
				}
				frame_full.payload_length = (((uint64_t)data[0] << 56) & 0xFF) |
											(((uint64_t)data[1] << 48) & 0xFF) |
											(((uint64_t)data[2] << 40) & 0xFF) |
											(((uint64_t)data[3] << 32) & 0xFF) |
											(((uint64_t)data[4] << 24) & 0xFF) |
											(((uint64_t)data[5] << 16) & 0xFF) |
											(((uint64_t)data[6] << 8) & 0xFF) |
											((uint64_t)data[7] & 0xFF);
			}
			else
			{
				ESP_LOGW(TAG,
					"Wrong payload_length");	
			}
			/* We only accept the incoming packet length that is smaller than the max_len (or it will overflow the buffer!) */
//			if (frame->len > max_len) {
//				ESP_LOGW(TAG, "WS Message too long");
//			}
			
			ESP_LOGI(TAG,
				"Payload length %d",
				(uint32_t)frame_full.payload_length);
			
			if ((frame_full.payload_length != 0))
			{
				if (frame_full.frame_header.mask)
				{
					ret_r = recv(accept_sock, frame_full.mask_key, WS_MASK_LENGTH, 0);
					if (ret_r)
					{			
						ESP_LOGI(TAG,
							"Mask keys %d, %d, %d, %d",
							frame_full.mask_key[0],
							frame_full.mask_key[1],
							frame_full.mask_key[2],
							frame_full.mask_key[3]);
					
						frame_full.payload = heap_caps_malloc(frame_full.payload_length, MALLOC_CAP_8BIT);
						ret_r = recv(accept_sock, frame_full.payload, frame_full.payload_length, 0);
						if (ret_r)
						{						
							for (int i = 0; i < frame_full.payload_length; i++)
							{
								frame_full.payload[i] = frame_full.payload[i] ^ frame_full.mask_key[i % 4];
							}
						}
					}
				}
				else
				{
					ret_r = recv(accept_sock, frame_full.payload, frame_full.payload_length, 0);
				}				
			}

			switch (frame_full.frame_header.opcode) 
			{
			case WS_OP_TXT:
				read_ws_text(accept_sock, frame_full.payload, frame_full.payload_length);
				break;
			case WS_OP_BIN:	
				read_ws_binary(accept_sock, (uint8_t*)frame_full.payload, frame_full.payload_length);
				break;
			case WS_OP_CON:
				//TODO processing of multi continuation frame
				break;
			case WS_OP_PIN:
				read_ws_ping(accept_sock, (uint8_t*)frame_full.payload, frame_full.payload_length);
				break;
			case WS_OP_PON:
				read_ws_pong(accept_sock, (uint8_t*)frame_full.payload, frame_full.payload_length);
				break;
			case WS_OP_CLS:
				read_ws_close(accept_sock, (uint8_t*)frame_full.payload, frame_full.payload_length);
				heap_caps_free(frame_full.payload);
				ESP_LOGI(TAG, "Websocket closed by client");
				close(accept_sock);
				vTaskDelete(NULL);
				break;
			default: break;
			}
			heap_caps_free(frame_full.payload);
		}
	}
}
//---------------------------------------------------------------
static void tcp_thread(void *arg)
{
	int sock, accept_sock, ret;
	struct sockaddr_in address, remotehost;
	socklen_t sockaddrsize;
	int buflen = 1024;
	char buf[1024] = { };
	char *istr;
	size_t len;
	if ((sock = socket(AF_INET, SOCK_STREAM, 0)) >= 0)
	{
		address.sin_family = AF_INET;
		address.sin_port = htons(WS_PORT);
		address.sin_addr.s_addr = INADDR_ANY;
		if (bind(sock, (struct sockaddr *)&address, sizeof(address)) ==  0)
		{
			listen(sock, 5);
			ESP_LOGI(TAG, "WebSocket Server listening on port %d", WS_PORT);
			for (;;)
			{
				accept_sock = accept(sock, (struct sockaddr *)&remotehost, (socklen_t *)&sockaddrsize);
				if (accept_sock >= 0)
				{
					ESP_LOGI(TAG, "New connection accepted");

					ret = recv(accept_sock, buf, buflen, 0);

					if (ret > 0)
					{
						if ((ret >= 5) && (strncmp(buf, "GET /", 5) == 0))
						{
							ESP_LOGI(TAG, "Received WebSocket handshake request");
							istr = strstr(buf, "Sec-WebSocket-Key:");
							istr[43] = 0;
							strcpy(str_buf, "HTTP/1.1 101 Switching Protocols\r\n");
							strcat(str_buf, "Upgrade: websocket\r\n");
							strcat(str_buf, "Connection: Upgrade\r\n");
							strcat(str_buf, "Sec-WebSocket-Accept: ");
							sprintf(str_key, "%s%s", istr + 19, guid_str);
							mbedtls_sha1((unsigned char *)str_key, 60, hash);
							mbedtls_base64_encode((unsigned char *)str_buf2, 100, &len, hash, 20);
							str_buf2[len] = 0;
							strcat(str_buf, (char*)str_buf2);
							strcat(str_buf, "\r\n\r\n");
								
							write(accept_sock, (const unsigned char*)(str_buf), strlen(str_buf));
								
							BaseType_t task_code = xTaskCreate(client_connection, 
								"client_connection", 
								DEFAULT_THREAD_STACKSIZE * 2,
								(void*)&accept_sock,
								TASK_NORMAL_PRIORITY, 
								NULL);		
							
							/*xTaskCreate(sending_thread, 
								"sending_thread", 
								2048, 
								(void*)&accept_sock, 
								5, 
								NULL);*/
							
							if (task_code == pdPASS)
							{
								ESP_LOGI(TAG,
									"New client thread created with connID = %d, IP: %u.%u.%u.%u:%u", 
									accept_sock,
									(uint8_t)remotehost.sin_addr.s_addr, 
									(uint8_t)(remotehost.sin_addr.s_addr >> 8), 
									(uint8_t)(remotehost.sin_addr.s_addr >> 16),
									(uint8_t)(remotehost.sin_addr.s_addr >> 24), 
									(uint16_t)remotehost.sin_port);
							}
						}
					}
					else
					{
						close(accept_sock);
						break;
					}					
				}				
			}
			close(sock);
			vTaskDelete(NULL);
		}
		close(sock);
		vTaskDelete(NULL);
	}
	close(sock);
	vTaskDelete(NULL);
}

static void sending_thread(void *argument)
{	
	int accept_sock = *(int*)argument;
	err_t ret_w = 0;
	cJSON *messageSend = cJSON_CreateObject();
	cJSON_AddNumberToObject(messageSend, "PWM_Speed_1", 1234);
	char* stringSend = NULL;
	for (;;)
	{		
		stringSend = cJSON_PrintUnformatted(messageSend);
		ESP_LOGI(TAG, "Server sending JSON length %d", strlen(stringSend));
		ret_w = websocket_write(accept_sock, WS_OP_TXT, stringSend, strlen(stringSend));
		if (ret_w == ESP_FAIL)
		{		
			ESP_LOGI(TAG, "Sending thread terminated");
			close(accept_sock);
			vTaskDelete(NULL);
		}
		free(stringSend);
		vTaskDelay(pdMS_TO_TICKS(2000));
	}
}

void websocket_server_init()
{
	xTaskCreate(tcp_thread, "websocket_server", 4096, NULL, 5, NULL);
}