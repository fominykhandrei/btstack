/*
 * Copyright (C) 2014 BlueKitchen GmbH
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the copyright holders nor the names of
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 * 4. Any redistribution, use, or modification is done solely for
 *    personal benefit and not for any commercial purpose or for
 *    monetary gain.
 *
 * THIS SOFTWARE IS PROVIDED BY BLUEKITCHEN GMBH AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL BLUEKITCHEN
 * GMBH OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
 * THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * Please inquire about commercial licensing options at
 * contact@bluekitchen-gmbh.com
 *
 */


/*
 *  btp_le_auodo.h
 *  BTP Client API
 */

#ifndef BTPCLIENT_H
#define BTPCLIENT_H

#include "btstack_run_loop.h"
#include "btstack_defines.h"
#include "btstack.h"

#include <stdint.h>

#if defined __cplusplus
extern "C" {
#endif

// TODO: hide globals from btpclient
extern uint8_t  response_service_id;
extern uint8_t  response_op;
extern uint16_t response_len;
extern uint8_t  response_buffer[512];
extern hci_con_handle_t remote_handle;

#define MESSAGE(format, ...) log_info(format, ## __VA_ARGS__); printf(format "\n", ## __VA_ARGS__)

void btp_append_uint8(uint8_t uint);
void btp_append_uint16(uint16_t uint);
void btp_append_uint24(uint32_t uint);
void btp_append_uint32(uint32_t uint);
void btp_append_blob(uint16_t len, const uint8_t * data);
void btp_append_uuid(uint16_t uuid16, const uint8_t * uuid128);
void btp_append_service(gatt_client_service_t * service);
void btp_append_remote_address(void);

void btp_send(uint8_t service_id, uint8_t opcode, uint8_t controller_index, uint16_t length, const uint8_t *data);

#if defined __cplusplus
}
#endif

#endif // BTPCLIENT_H