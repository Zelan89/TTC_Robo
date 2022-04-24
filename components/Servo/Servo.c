/* servo control
   
*/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "esp_log.h"
#include "esp_system.h"
#include "esp_err.h"

#include "esp8266/gpio_register.h"
#include "esp8266/pin_mux_register.h"

#include "driver/pwm.h"
#include "driver/gpio.h"
#include "driver/adc.h"
#include "math.h"

#include "Servo.h"

#define PWM_PIN_BLDC_DOWN				12
#define PWM_PIN_BLDC_LEFT				13
#define PWM_PIN_BLDC_RIGHT				14
#define PWM_PIN_SERVO_X					4
#define PWM_PIN_SERVO_Y					5
#define PWM_PIN_SERVO_FEEDER			2

#define REVERSE_PIN_BLDC_DOWN			0
#define REVERSE_PIN_BLDC_LEFT			15
#define REVERSE_PIN_BLDC_RIGHT			16
#define GPIO_OUTPUT_PIN_SEL				((1ULL<<REVERSE_PIN_BLDC_DOWN) | (1ULL<<REVERSE_PIN_BLDC_LEFT) | (1ULL<<REVERSE_PIN_BLDC_RIGHT))
//#define BALL_PROXIMITY_SENSOR_PIN		ADC0
//#define GPIO_INPUT_PIN_SEL  (1ULL<<BALL_PROXIMITY_SENSOR_PIN)

#define PWM_BLDC_DOWN_CHANNEL			0
#define PWM_BLDC_LEFT_CHANNEL			1
#define PWM_BLDC_RIGHT_CHANNEL			2
#define PWM_BLDC_SERVO_X_CHANNEL		3
#define PWM_BLDC_SERVO_Y_CHANNEL		4
#define PWM_BLDC_SERVO_FEEDER_CHANNEL	5

#define PWM_CHANNEL_NUM					6

#define MIN_ANGLE_DUTY					1000
#define MAX_ANGLE_DUTY					19000
#define MIN_ANGLE_DEGREE			   -30
#define MAX_ANGLE_DEGREE				30
#define MIN_BPM							0
#define MAX_BPM							100

#define PWM_PERIOD						20000		// PWM period 20ms - 50hz (20000 uS)

QueueHandle_t servoPositionQueue;
QueueHandle_t servoBLDCQueue;
QueueHandle_t servoFeederQueue;
QueueHandle_t servoControlQueue;

static const char *TAG = "servo_control";
// pwm pin number
const uint32_t pin_num[PWM_CHANNEL_NUM] = {
	PWM_PIN_BLDC_DOWN,
	PWM_PIN_BLDC_LEFT,
	PWM_PIN_BLDC_RIGHT,
	PWM_PIN_SERVO_X,
	PWM_PIN_SERVO_Y,
	PWM_PIN_SERVO_FEEDER	
};

uint32_t duty[PWM_CHANNEL_NUM] = { 0 };
float phase[PWM_CHANNEL_NUM] = { 0 };
enum trainingProgram
{
	MANUAL = 0,
	RANDOM,
	BOX,
	PROGRAMM
};


struct speed
{
	uint16_t down;
	uint16_t left;
	uint16_t right;
};

static void ramp_speed(uint32_t speed_sp, uint32_t *ramped_speed, float rampKi)
{
	int32_t err, err_abs;
	int8_t sign;

	err = (int32_t)speed_sp - (int32_t)*ramped_speed;
	err_abs = abs(err);
	if (err_abs > rampKi)
	{
		sign = err / err_abs;
		*ramped_speed += (rampKi * sign);
	}
	else
	{
		*ramped_speed = speed_sp;
	}
}

static void servo_BLDC(void *argument)
{
	int16_t speed[3] = { };

	servoBLDCQueue = xQueueCreate(10, sizeof(speed));
	if (servoBLDCQueue == NULL)
	{
		ESP_LOGE(TAG, "Create servoBLDCQueue fail");
	}
	
	while (1) 
	{
		BaseType_t r = xQueueReceive(servoBLDCQueue, &speed, portMAX_DELAY);
		
		if (r == pdTRUE)
		{
			pwm_set_duty(PWM_BLDC_DOWN_CHANNEL, abs(speed[0]));
			pwm_set_duty(PWM_BLDC_LEFT_CHANNEL, abs(speed[1]));
			pwm_set_duty(PWM_BLDC_RIGHT_CHANNEL, abs(speed[2]));
			pwm_start();
		}
	}
}

static void servo_position(void *argument)
{
	uint8_t position[2] = { };
	uint16_t duty[2] = { };

	servoPositionQueue = xQueueCreate(10, sizeof(position));
	if (servoPositionQueue == NULL) 
	{
		ESP_LOGE(TAG, "Create servoPositionQueue fail");
	}
	
	while (1) 
	{
		BaseType_t r = xQueueReceive(servoPositionQueue, &position, portMAX_DELAY);
		if (r == pdTRUE)
		{
			duty[0] = ((float)position[0] / 100.0) * (MAX_ANGLE_DUTY - MIN_ANGLE_DUTY) + MIN_ANGLE_DUTY;
			duty[1] = ((float)position[1] / 100.0) * (MAX_ANGLE_DUTY - MIN_ANGLE_DUTY) + MIN_ANGLE_DUTY;
			pwm_set_duty(PWM_BLDC_SERVO_X_CHANNEL, duty[0]);
			pwm_set_duty(PWM_BLDC_SERVO_Y_CHANNEL, duty[1]);
			pwm_start();
		}
	}
}

static void servo_feeder(void *argument)
{
	uint32_t ballFrequency = MIN_BPM;
	uint32_t rampedFrequency = 0;
	uint32_t dutySetpoint = 0;
	servoFeederQueue = xQueueCreate(10, sizeof(ballFrequency));
	if (servoFeederQueue == NULL) 
	{
		ESP_LOGE(TAG, "Create servoFeederQueue fail");
	}
	
	while (1) 
	{
		BaseType_t r = xQueueReceive(servoFeederQueue, &ballFrequency, portMAX_DELAY);
		if (r == pdTRUE)
		{
			ESP_LOGI(TAG, "New BPM setpoint received %d", ballFrequency);
			
			/*if (ballFrequency < MIN_BPM)
			{
				ballFrequency = MIN_BPM;
			}
			else */
			if (ballFrequency > MAX_BPM)
			{
				ballFrequency = MAX_BPM;
			}	
		}

		while (ballFrequency != rampedFrequency)
		{
			ramp_speed(ballFrequency, &rampedFrequency, 1);
			dutySetpoint = ((float)rampedFrequency / 100.0) * (MAX_ANGLE_DUTY - MIN_ANGLE_DUTY) + MIN_ANGLE_DUTY;
			pwm_set_duty(PWM_BLDC_SERVO_FEEDER_CHANNEL, dutySetpoint);
			ESP_ERROR_CHECK(pwm_start());
			ESP_LOGI(TAG, "Ball frequency %d", dutySetpoint);
			vTaskDelay(pdMS_TO_TICKS(5));
		}
	}
}
void manual_control()
{
	
}
void random_control()
{
	
}

static void control(void *argument)
{
	enum trainingProgram programm = 0;
	prgogrammSp programmSp = { 0 };
	for (;;)
	{			
		BaseType_t r = xQueueReceive(servoFeederQueue, &programmSp.BPM, portMAX_DELAY);
		if (r == pdTRUE)
		{
			ESP_LOGI(TAG, "New BPM setpoint received %d", programmSp.BPM);
			
			/*if (programmSp.BPM < MIN_BPM)
			{
				programmSp.BPM = MIN_BPM;
			}
			else */
			if (programmSp.BPM > MAX_BPM)
			{
				programmSp.BPM = MAX_BPM;
			}	
		}
		
		switch (programm)
		{
		case MANUAL:
			manual_control();
			break;
		case RANDOM:
			random_control();
			break;
		case BOX:
			break;
		case PROGRAMM:
			break;
		default:break;
		}
		vTaskDelay(pdMS_TO_TICKS(100));
	}
}

void gpio_adc_init(void)
{
	uint16_t adc_data;
	
	gpio_config_t io_conf;
	//disable interrupt
	io_conf.intr_type = GPIO_INTR_DISABLE;
	//set as output mode
	io_conf.mode = GPIO_MODE_OUTPUT;
	//bit mask of the pins that you want to set,e.g.GPIO15/16
	io_conf.pin_bit_mask = GPIO_OUTPUT_PIN_SEL;
	//disable pull-down mode
	io_conf.pull_down_en = 0;
	//disable pull-up mode
	io_conf.pull_up_en = 0;
	//configure GPIO with the given settings
	gpio_config(&io_conf);
	
	// 1. init adc
	adc_config_t adc_config;

	// Depend on menuconfig->Component config->PHY->vdd33_const value
	// When measuring system voltage(ADC_READ_VDD_MODE), vdd33_const must be set to 255.
	adc_config.mode = ADC_READ_TOUT_MODE;
	adc_config.clk_div = 8;  // ADC sample collection clock = 80MHz/clk_div = 10MHz
	ESP_ERROR_CHECK(adc_init(&adc_config));

	/*while (1) 
	{
		vTaskDelay(1000 / portTICK_RATE_MS);
		if (ESP_OK == adc_read(&adc_data)) {
			ESP_LOGI(TAG, "adc read: %d\r\n", adc_data);
		}
	}*/
}

void servo_init()
{
	//Initilize all servo channels with 0 duty
	pwm_init(PWM_PERIOD, duty, PWM_CHANNEL_NUM, pin_num);
	pwm_set_phases(phase);
	pwm_start();	
	gpio_adc_init();
	xTaskCreate(servo_position, "servo_position", 1024, NULL, 5, NULL);
	xTaskCreate(servo_BLDC, "servo_BLDC", 1024, NULL, 5, NULL);
	xTaskCreate(servo_feeder, "servo_feeder", 1024, NULL, 5, NULL);
	//xTaskCreate(control, "servo_control", 256, NULL, 5, NULL);
}