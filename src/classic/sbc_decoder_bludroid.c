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
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL MATTHIAS
 * RINGWALD OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
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
 
// *****************************************************************************
//
// SBC decoder tests
//
// *****************************************************************************

#include "btstack_config.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include "sbc_decoder.h"
#include "oi_codec_sbc.h"
#include "oi_assert.h"

#include "btstack.h"

#define SBC_MAX_CHANNELS 2
#define DECODER_DATA_SIZE (SBC_MAX_CHANNELS*SBC_MAX_BLOCKS*SBC_MAX_BANDS * 2 + SBC_CODEC_MIN_FILTER_BUFFERS*SBC_MAX_BANDS*SBC_MAX_CHANNELS * 2)
typedef struct {
    OI_UINT32 bytes_in_frame_buffer;
    OI_CODEC_SBC_DECODER_CONTEXT decoder_context;
    
    uint8_t frame_buffer[SBC_MAX_FRAME_LEN];
    int16_t pcm_data[SBC_MAX_CHANNELS * SBC_MAX_BANDS * SBC_MAX_BLOCKS];
    uint32_t pcm_bytes;
    OI_UINT32 decoder_data[(DECODER_DATA_SIZE+3)/4]; 
} bludroid_decoder_state_t;

static sbc_decoder_state_t * sbc_state_singelton = NULL;
static bludroid_decoder_state_t bd_state;

int sbc_decoder_num_samples_per_frame(sbc_decoder_state_t * state){
    bludroid_decoder_state_t * decoder_state = (bludroid_decoder_state_t *) state->decoder_state;
    return decoder_state->decoder_context.common.frameInfo.nrof_blocks * decoder_state->decoder_context.common.frameInfo.nrof_subbands;
}

int sbc_decoder_num_channels(sbc_decoder_state_t * state){
    bludroid_decoder_state_t * decoder_state = (bludroid_decoder_state_t *) state->decoder_state;
    return decoder_state->decoder_context.common.frameInfo.nrof_channels;
}

int sbc_decoder_sample_rate(sbc_decoder_state_t * state){
    bludroid_decoder_state_t * decoder_state = (bludroid_decoder_state_t *) state->decoder_state;
    return decoder_state->decoder_context.common.frameInfo.frequency;
}


void OI_AssertFail(char* file, int line, char* reason){
    printf("AssertFail file %s, line %d, reason %s\n", file, line, reason);
}

void sbc_decoder_init(sbc_decoder_state_t * state, sbc_mode_t mode, void (*callback)(int16_t * data, int num_samples, int num_channels, int sample_rate, void * context), void * context){
    if (sbc_state_singelton && sbc_state_singelton != state ){
        log_error("SBC decoder: different sbc decoder state is allready registered");
    } 
    OI_STATUS status;
    switch (mode){
        case SBC_MODE_STANDARD:
            status = OI_CODEC_SBC_DecoderReset(&(bd_state.decoder_context), bd_state.decoder_data, sizeof(bd_state.decoder_data), 2, 1, FALSE);
            break;
        case SBC_MODE_mSBC:
            status = OI_CODEC_mSBC_DecoderReset(&(bd_state.decoder_context), bd_state.decoder_data, sizeof(bd_state.decoder_data));
            break;
    }

    if (status != 0){
        log_error("SBC decoder: error during reset %d\n", status);
    }
    
    sbc_state_singelton = state;
    bd_state.bytes_in_frame_buffer = 0;
    bd_state.pcm_bytes = sizeof(bd_state.pcm_data);

    state->handle_pcm_data = callback;
    state->mode = mode;
    state->context = context;
    state->decoder_state = &bd_state;
}

static void append_received_sbc_data(bludroid_decoder_state_t * state, uint8_t * buffer, int size){
    int numFreeBytes = sizeof(state->frame_buffer) - state->bytes_in_frame_buffer;

    if (size > numFreeBytes){
        log_error("SBC data: more bytes read %u than free bytes in buffer %u", size, numFreeBytes);
    }

    memcpy(state->frame_buffer + state->bytes_in_frame_buffer, buffer, size);
    state->bytes_in_frame_buffer += size;
}

void sbc_decoder_process_data(sbc_decoder_state_t * state, uint8_t * buffer, int size){
    int bytes_to_process = size;
    bludroid_decoder_state_t * bd_decoder_state = (bludroid_decoder_state_t*)state->decoder_state;

    while (bytes_to_process > 0){
        int space_in_frame_buffer = sizeof(bd_decoder_state->frame_buffer) - bd_decoder_state->bytes_in_frame_buffer;
        int bytes_to_append = space_in_frame_buffer > bytes_to_process ? bytes_to_process : space_in_frame_buffer;
        // fill frame buffer to max capacity
        append_received_sbc_data(bd_decoder_state, buffer, bytes_to_append);

        // process whole buffer, possibly more then one frame
        while (1){
            uint16_t bytes_in_buffer_before = bd_decoder_state->bytes_in_frame_buffer;
            const OI_BYTE *frame_data = bd_decoder_state->frame_buffer;
            OI_STATUS status = OI_CODEC_SBC_DecodeFrame(&(bd_decoder_state->decoder_context), 
                                                        &frame_data, 
                                                        &(bd_decoder_state->bytes_in_frame_buffer), 
                                                        bd_decoder_state->pcm_data, 
                                                        &(bd_decoder_state->pcm_bytes));

            if (status == OI_CODEC_SBC_CHECKSUM_MISMATCH){
                // advance at least one byte
                bd_decoder_state->bytes_in_frame_buffer--;
            }

            uint16_t bytes_processed = bytes_in_buffer_before - bd_decoder_state->bytes_in_frame_buffer;
            // log_info("sbc_decoder_process_data: decode status %u, processed %u, left %u", status, bytes_processed, bd_decoder_state->bytes_in_frame_buffer);
            memmove(bd_decoder_state->frame_buffer, bd_decoder_state->frame_buffer + bytes_processed, bd_decoder_state->bytes_in_frame_buffer);

            switch(status){
                case 0:
                    state->handle_pcm_data(bd_decoder_state->pcm_data, 
                                        sbc_decoder_num_samples_per_frame(state), 
                                        sbc_decoder_num_channels(state), 
                                        sbc_decoder_sample_rate(state), state->context);
                    continue;
                case OI_CODEC_SBC_NOT_ENOUGH_HEADER_DATA:
                case OI_CODEC_SBC_NOT_ENOUGH_BODY_DATA:
                case OI_CODEC_SBC_NO_SYNCWORD:
                    break;
                case OI_CODEC_SBC_CHECKSUM_MISMATCH:
                    printf("Frame decode error: OI_CODEC_SBC_CHECKSUM_MISMATCH\n");
                    break;
                default:
                    printf("Frame decode error: %d\n", status);
                    break;
            }
            break;
        }
            
        buffer     += bytes_to_append;
        bytes_to_process -= bytes_to_append;
    }
}
