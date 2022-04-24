#pragma once
/**
 * @section License
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2017, Thomas Barth, barth-dev.de
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy,
 * modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */


#ifndef	_WEBSOCKET_TASK_H_
#define _WEBSOCKET_TASK_H_

#include "lwip/api.h"

typedef enum {
	WStype_ERROR,
	WStype_DISCONNECTED,
	WStype_CONNECTED,
	WStype_TEXT,
	WStype_BIN,
	WStype_FRAGMENT_TEXT_START,
	WStype_FRAGMENT_BIN_START,
	WStype_FRAGMENT,
	WStype_FRAGMENT_FIN,
	WStype_PING,
	WStype_PONG,
} WStype_t;

typedef enum {
	WS_OP_CON = 0x0,
	/*!< Continuation Frame*/
	WS_OP_TXT = 0x1,
	/*!< Text Frame*/
	WS_OP_BIN = 0x2,
	/*!< Binary Frame*/
	WS_OP_CLS = 0x8,
	/*!< Connection Close Frame*/
	WS_OP_PIN = 0x9,
	/*!< Ping Frame*/
	WS_OP_PON = 0xA,  
} WS_OPCODES;

#pragma pack(push,1)
/** \brief Websocket frame header type*/
typedef struct {
	WS_OPCODES opcode : 4;
	uint8_t reserved : 3;
	bool FIN : 1;
	uint8_t payload_code : 7;
	bool mask : 1;
} WS_frame_header_t;
#pragma pack(pop)
/** \brief Websocket frame type*/
typedef struct
{
	WS_frame_header_t	frame_header;
	uint64_t			payload_length;
	char				mask_key[4];
	char*				payload;
} WS_frame_full_t;


/**
 * \brief Send data to the websocket client
 *
 * \return 	#ERR_VAL: 	Payload length exceeded 2^7 bytes.
 * 			#ERR_CONN:	There is no open connection
 * 			#ERR_OK:	Header and payload send
 * 			all other values: derived from #netconn_write (sending frame header or payload)
 */
err_t websocket_write(int conn, WS_OPCODES opcode, char* p_data, size_t length);

/**
 * \brief WebSocket Server task
 */
void ws_server(void *pvParameters);

void websocket_server_init();
#endif /* _WEBSOCKET_TASK_H_ */
