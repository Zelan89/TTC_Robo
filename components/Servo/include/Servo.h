#pragma once

typedef struct joystick_t 
{
	float angle;
	float distance;
} joystick;

typedef struct coordinates_t
{
	float x;
	float y;
} coordinates;

typedef struct prgogrammSp_t
{
	uint32_t BPM;
	joystick joy;
	coordinates coord;
} prgogrammSp;

typedef struct servoSp_t
{
	uint32_t feederDuty;
	uint32_t servoDuty[2];
	uint32_t shooterDuty[3];
} servoSp;
void servo_init();