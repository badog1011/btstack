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
// Minimal test for HSP Headset (!! UNDER DEVELOPMENT !!)
//
// *****************************************************************************

#include "btstack-config.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <net/if_arp.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

#include <btstack/hci_cmds.h>
#include <btstack/run_loop.h>
#include <btstack/sdp_util.h>

#include "hci.h"
#include "l2cap.h"
#include "rfcomm.h"
#include "sdp.h"
#include "debug.h"
#include "hfp_hf.h"
#include "stdin_support.h"

const uint32_t   hfp_service_buffer[150/4]; // implicit alignment to 4-byte memory address
const uint8_t    rfcomm_channel_nr = 1;
const char hfp_hf_service_name[] = "BTstack HFP HF Test";

static bd_addr_t pts_addr = {0x00,0x1b,0xDC,0x07,0x32,0xEF};
static bd_addr_t phone_addr = {0xD8,0xBb,0x2C,0xDf,0xF1,0x08};

static bd_addr_t device_addr;
static uint8_t codecs[1] = {HFP_CODEC_CVSD};
static uint16_t indicators[1] = {0x01};

char cmd;

uint8_t hfp_enable_extended_audio_gateway_error_report = 1;
uint8_t hfp_enable_status_update_for_all_ag_indicators = 1;

// prototypes
static void show_usage();

static void reset_pst_flags(){
    hfp_enable_extended_audio_gateway_error_report = 1;
    hfp_enable_status_update_for_all_ag_indicators = 1;
}

// Testig User Interface 
static void show_usage(void){
    printf("\n--- Bluetooth HFP Hands-Free (HF) unit Test Console ---\n");
    printf("---\n");
    
    printf("a - establish HFP connection to PTS module\n");
    printf("A - release HFP connection to PTS module\n");
    
    printf("b - establish AUDIO connection\n");
    printf("B - release AUDIO connection\n");
    
    printf("z - establish HFP connection to local mac\n");
    printf("Z - release HFP connection to local mac\n");
    
    if (hfp_enable_status_update_for_all_ag_indicators){
        printf("d - enable registration status update\n");
    } else {
        printf("d - disable registration status update\n");
    }

    printf("e - enable HFP AG registration status update for individual indicators\n");
    
    printf("f - query network operator\n");
    if (hfp_enable_extended_audio_gateway_error_report){
        printf("g - enable reporting of the extended AG error result code\n");
    } else {
        printf("g - disable reporting of the extended AG error result code\n");
    }
    printf("---\n");
    printf("Ctrl-c - exit\n");
    printf("---\n");
}

static int stdin_process(struct data_source *ds){
    read(ds->fd, &cmd, 1);
    switch (cmd){
        case 'a':
            memcpy(device_addr, pts_addr, 6);
            printf("Establish HFP service level connection to PTS module %s...\n", bd_addr_to_str(device_addr));
            hfp_hf_establish_service_level_connection(device_addr);
            break;
        case 'A':
            printf("Release HFP service level connection.\n");
            hfp_hf_release_service_level_connection(device_addr);
            break;
        case 'z':
            memcpy(device_addr, phone_addr, 6);
            printf("Establish HFP service level connection to %s...\n", bd_addr_to_str(device_addr));
            hfp_hf_establish_service_level_connection(device_addr);
            break;
        case 'Z':
            printf("Release HFP service level connection to %s...\n", bd_addr_to_str(device_addr));
            hfp_hf_release_service_level_connection(device_addr);
            break;
        case 'b':
            printf("Establish Audio connection %s...\n", bd_addr_to_str(device_addr));
            hfp_hf_establish_audio_connection(device_addr);
            break;
        case 'B':
            printf("Release Audio connection.\n");
            hfp_hf_release_audio_connection(device_addr);
            break;
        
        case 'd':
            if (hfp_enable_status_update_for_all_ag_indicators){
                printf("Enable HFP AG registration status update.\n");
            } else {
                printf("Disable HFP AG registration status update.\n");
            }
            hfp_hf_enable_status_update_for_all_ag_indicators(device_addr, hfp_enable_status_update_for_all_ag_indicators);
            hfp_enable_status_update_for_all_ag_indicators = !hfp_enable_status_update_for_all_ag_indicators;
            break;
        case 'e':
            printf("Enable HFP AG registration status update for individual indicators.\n");
            hfp_hf_enable_status_update_for_individual_ag_indicators(device_addr, 63);
            break;
        case 'f':
            printf("Query network operator.\n");
            hfp_hf_query_operator_selection(device_addr);
            break;
        case 'g':
            if (hfp_enable_extended_audio_gateway_error_report){
                printf("Enable reporting of the extended AG error result code.\n");
            } else {
                printf("Disable reporting of the extended AG error result code.\n");
            }
            hfp_hf_enable_report_extended_audio_gateway_error_result_code(device_addr, hfp_enable_extended_audio_gateway_error_report);
            hfp_enable_extended_audio_gateway_error_report = !hfp_enable_extended_audio_gateway_error_report;
            break;
        default:
            show_usage();
            break;
    }

    return 0;
}


void packet_handler(uint8_t * event, uint16_t event_size){
    if (event[0] != HCI_EVENT_HFP_META) return;
    if (event[3] && event[2] != HFP_SUBEVENT_EXTENDED_AUDIO_GATEWAY_ERROR){
        printf("ERROR, status: %u\n", event[3]);
        return;
    }
    switch (event[2]) {   
        case HFP_SUBEVENT_SERVICE_LEVEL_CONNECTION_ESTABLISHED:
            printf("Service level connection established.\n\n");
            break;
        case HFP_SUBEVENT_SERVICE_LEVEL_CONNECTION_RELEASED:
            printf("Service level connection released.\n\n");
            reset_pst_flags();
            break;
        case HFP_SUBEVENT_COMPLETE:
            switch (cmd){
                case 'd':
                    printf("HFP AG registration status update enabled.\n");
                    break;
                case 'e':
                    printf("HFP AG registration status update for individual indicators set.\n");
                default:
                    break;
            }
            break;
        case HFP_SUBEVENT_AG_INDICATOR_STATUS_CHANGED:
            printf("AG_INDICATOR_STATUS_CHANGED, AG indicator index: %d, status: %d\n", event[4], event[5]);
            break;
        case HFP_SUBEVENT_NETWORK_OPERATOR_CHANGED:
            printf("NETWORK_OPERATOR_CHANGED, operator mode: %d, format: %d, name: %s\n", event[4], event[5], (char *) &event[6]);
            break;
        case HFP_SUBEVENT_EXTENDED_AUDIO_GATEWAY_ERROR:
            if (event[4])
            printf("EXTENDED_AUDIO_GATEWAY_ERROR_REPORT, status : %d\n", event[3]);
            break;
        default:
            printf("event not handled %u\n", event[2]);
            break;
    }
}


int btstack_main(int argc, const char * argv[]){
    // init L2CAP
    l2cap_init();
    rfcomm_init();
    
    // hfp_hf_init(rfcomm_channel_nr, HFP_DEFAULT_HF_SUPPORTED_FEATURES, codecs, sizeof(codecs), indicators, sizeof(indicators)/sizeof(uint16_t), 1);
    hfp_hf_init(rfcomm_channel_nr, 438, indicators, sizeof(indicators)/sizeof(uint16_t), 1);
    hfp_hf_set_codecs(codecs, sizeof(codecs));
    
    hfp_hf_register_packet_handler(packet_handler);

    sdp_init();
    // init SDP, create record for SPP and register with SDP
    memset((uint8_t *)hfp_service_buffer, 0, sizeof(hfp_service_buffer));
    hfp_hf_create_sdp_record((uint8_t *)hfp_service_buffer, rfcomm_channel_nr, hfp_hf_service_name, 0);
    sdp_register_service_internal(NULL, (uint8_t *)hfp_service_buffer);

    // turn on!
    hci_power_control(HCI_POWER_ON);
    
    btstack_stdin_setup(stdin_process);
    // printf("Establishing HFP connection to %s...\n", bd_addr_to_str(phone_addr));
    // hfp_hf_connect(phone_addr);
    return 0;
}
