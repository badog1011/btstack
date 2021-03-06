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
// Minimal setup for HFP Audio Gateway (AG) unit (!! UNDER DEVELOPMENT !!)
//
// *****************************************************************************

#include "btstack-config.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#include <btstack/hci_cmds.h>
#include <btstack/run_loop.h>

#include "hci.h"
#include "btstack_memory.h"
#include "hci_dump.h"
#include "l2cap.h"
#include "sdp_query_rfcomm.h"
#include "sdp.h"
#include "debug.h"

#define HFP_HF_FEATURES_SIZE 10
#define HFP_AG_FEATURES_SIZE 12


static const char * hfp_hf_features[] = {
    "EC and/or NR function",
    "Three-way calling",
    "CLI presentation capability",
    "Voice recognition activation",
    "Remote volume control",

    "Enhanced call status",
    "Enhanced call control",
    
    "Codec negotiation",
    
    "HF Indicators",
    "eSCO S4 (and T2) Settings Supported",
    "Reserved for future definition"
};

static const char * hfp_ag_features[] = {
    "Three-way calling",
    "EC and/or NR function",
    "Voice recognition function",
    "In-band ring tone capability",
    "Attach a number to a voice tag",
    "Ability to reject a call",
    "Enhanced call status",
    "Enhanced call control",
    "Extended Error Result Codes",
    "Codec negotiation",
    "HF Indicators",
    "eSCO S4 (and T2) Settings Supported",
    "Reserved for future definition"
};

static int hfp_generic_status_indicators_nr = 0;
static hfp_generic_status_indicator_t hfp_generic_status_indicators[HFP_MAX_NUM_HF_INDICATORS];

static linked_list_t hfp_connections = NULL;

hfp_generic_status_indicator_t * get_hfp_generic_status_indicators(){
    return (hfp_generic_status_indicator_t *) &hfp_generic_status_indicators;
}

int get_hfp_generic_status_indicators_nr(){
    return hfp_generic_status_indicators_nr;
}

void set_hfp_generic_status_indicators(hfp_generic_status_indicator_t * indicators, int indicator_nr){
    if (indicator_nr > HFP_MAX_NUM_HF_INDICATORS) return;
    hfp_generic_status_indicators_nr = indicator_nr;
    memcpy(hfp_generic_status_indicators, indicators, indicator_nr * sizeof(hfp_generic_status_indicator_t));
}

const char * hfp_hf_feature(int index){
    if (index > HFP_HF_FEATURES_SIZE){
        return hfp_hf_features[HFP_HF_FEATURES_SIZE];
    }
    return hfp_hf_features[index];
}

const char * hfp_ag_feature(int index){
    if (index > HFP_AG_FEATURES_SIZE){
        return hfp_ag_features[HFP_AG_FEATURES_SIZE];
    }
    return hfp_ag_features[index];
}

int send_str_over_rfcomm(uint16_t cid, char * command){
    // if (!rfcomm_can_send_packet_now(cid)) return 1;
    int err = rfcomm_send_internal(cid, (uint8_t*) command, strlen(command));
    if (err){
        printf("rfcomm_send_internal -> error 0X%02x", err);
    } 
    return err;
}

void hfp_set_codec(hfp_connection_t * context, uint8_t *packet, uint16_t size){
    // parse available codecs
    int pos = 0;
    int i;
    for (i=0; i<size; i++){
        pos+=8;
        if (packet[pos] > context->negotiated_codec){
            context->negotiated_codec = packet[pos];
        }
    }
    printf("Negotiated Codec 0x%02x\n", context->negotiated_codec);
}

// UTILS
int get_bit(uint16_t bitmap, int position){
    return (bitmap >> position) & 1;
}

int store_bit(uint32_t bitmap, int position, uint8_t value){
    if (value){
        bitmap |= 1 << position;
    } else {
        bitmap &= ~ (1 << position);
    }
    return bitmap;
}

int join(char * buffer, int buffer_size, uint8_t * values, int values_nr){
    if (buffer_size < values_nr * 3) return 0;
    int i;
    int offset = 0;
    for (i = 0; i < values_nr-1; i++) {
      offset += snprintf(buffer+offset, buffer_size-offset, "%d,", values[i]); // puts string into buffer
    }
    if (i<values_nr){
        offset += snprintf(buffer+offset, buffer_size-offset, "%d", values[i]);
    }
    return offset;
}

int join_bitmap(char * buffer, int buffer_size, uint32_t values, int values_nr){
    if (buffer_size < values_nr * 3) return 0;

    int i;
    int offset = 0;
    for (i = 0; i < values_nr-1; i++) {
      offset += snprintf(buffer+offset, buffer_size-offset, "%d,", get_bit(values,i)); // puts string into buffer
    }
    
    if (i<values_nr){
        offset += snprintf(buffer+offset, buffer_size-offset, "%d", get_bit(values,i));
    }
    return offset;
}

void hfp_emit_event(hfp_callback_t callback, uint8_t event_subtype, uint8_t value){
    if (!callback) return;
    uint8_t event[4];
    event[0] = HCI_EVENT_HFP_META;
    event[1] = sizeof(event) - 2;
    event[2] = event_subtype;
    event[3] = value; // status 0 == OK
    (*callback)(event, sizeof(event));
}


linked_list_t * hfp_get_connections(){
    return (linked_list_t *) &hfp_connections;
} 

hfp_connection_t * get_hfp_connection_context_for_rfcomm_cid(uint16_t cid){
    linked_list_iterator_t it;    
    linked_list_iterator_init(&it, hfp_get_connections());
    while (linked_list_iterator_has_next(&it)){
        hfp_connection_t * connection = (hfp_connection_t *)linked_list_iterator_next(&it);
        if (connection->rfcomm_cid == cid){
            return connection;
        }
    }
    return NULL;
}

hfp_connection_t * get_hfp_connection_context_for_bd_addr(bd_addr_t bd_addr){
    linked_list_iterator_t it;  
    linked_list_iterator_init(&it, hfp_get_connections());
    while (linked_list_iterator_has_next(&it)){
        hfp_connection_t * connection = (hfp_connection_t *)linked_list_iterator_next(&it);
        if (memcmp(connection->remote_addr, bd_addr, 6) == 0) {
            return connection;
        }
    }
    return NULL;
}

static hfp_connection_t * get_hfp_connection_context_for_handle(uint16_t handle){
    linked_list_iterator_t it;    
    linked_list_iterator_init(&it, hfp_get_connections());
    while (linked_list_iterator_has_next(&it)){
        hfp_connection_t * connection = (hfp_connection_t *)linked_list_iterator_next(&it);
        if (connection->con_handle == handle){
            return connection;
        }
    }
    return NULL;
}

void hfp_reset_context_flags(hfp_connection_t * context){
    context->wait_ok = 0;
    context->send_ok = 0;
    context->send_error = 0;

    context->keep_separator = 0;

    context->retrieve_ag_indicators = 0;        // HFP_CMD_INDICATOR, check if needed
    context->retrieve_ag_indicators_status = 0; 

    context->list_generic_status_indicators = 0;           // HFP_CMD_LIST_GENERIC_STATUS_INDICATOR
    context->retrieve_generic_status_indicators = 0;       // HFP_CMD_GENERIC_STATUS_INDICATOR
    context->retrieve_generic_status_indicators_state = 0; // HFP_CMD_GENERIC_STATUS_INDICATOR_STATE
    
    context->change_status_update_for_individual_ag_indicators = 0; 

    context->operator_name_format = 0;       
    context->operator_name = 0;              
    context->operator_name_changed = 0;      

    context->enable_extended_audio_gateway_error_report = 0;
    context->extended_audio_gateway_error = 0;

    // can come any time (here taken into account only after SLE),
    // if codec negotiation feature is set
    context->notify_ag_on_new_codecs = 0;
    
    // establish codecs connection
    context->trigger_codec_connection_setup = 0;
    context->remote_codec_received = 0;

    context->establish_audio_connection = 0; 
    context->release_audio_connection = 0; 
}

static hfp_connection_t * create_hfp_connection_context(){
    hfp_connection_t * context = btstack_memory_hfp_connection_get();
    if (!context) return NULL;
    // init state
    memset(context,0, sizeof(hfp_connection_t));

    context->state = HFP_IDLE;
    context->parser_state = HFP_PARSER_CMD_HEADER;
    context->command = HFP_CMD_NONE;
    context->negotiated_codec = HFP_CODEC_CVSD;
    
    context->enable_status_update_for_ag_indicators = 0xFF;

    context->generic_status_indicators_nr = hfp_generic_status_indicators_nr;
    memcpy(context->generic_status_indicators, hfp_generic_status_indicators, hfp_generic_status_indicators_nr * sizeof(hfp_generic_status_indicator_t));

    linked_list_add(&hfp_connections, (linked_item_t*)context);
    return context;
}

static void remove_hfp_connection_context(hfp_connection_t * context){
    linked_list_remove(&hfp_connections, (linked_item_t*)context);   
}

hfp_connection_t * provide_hfp_connection_context_for_bd_addr(bd_addr_t bd_addr){
    hfp_connection_t * context = get_hfp_connection_context_for_bd_addr(bd_addr);
    if (context) return  context;
    context = create_hfp_connection_context();
    memcpy(context->remote_addr, bd_addr, 6);
    return context;
}


/* @param suported_features
 * HF bit 0: EC and/or NR function (yes/no, 1 = yes, 0 = no)
 * HF bit 1: Call waiting or three-way calling(yes/no, 1 = yes, 0 = no)
 * HF bit 2: CLI presentation capability (yes/no, 1 = yes, 0 = no)
 * HF bit 3: Voice recognition activation (yes/no, 1= yes, 0 = no)
 * HF bit 4: Remote volume control (yes/no, 1 = yes, 0 = no)
 * HF bit 5: Wide band speech (yes/no, 1 = yes, 0 = no)
 */
 /* Bit position:
 * AG bit 0: Three-way calling (yes/no, 1 = yes, 0 = no)
 * AG bit 1: EC and/or NR function (yes/no, 1 = yes, 0 = no)
 * AG bit 2: Voice recognition function (yes/no, 1 = yes, 0 = no)
 * AG bit 3: In-band ring tone capability (yes/no, 1 = yes, 0 = no)
 * AG bit 4: Attach a phone number to a voice tag (yes/no, 1 = yes, 0 = no)
 * AG bit 5: Wide band speech (yes/no, 1 = yes, 0 = no)
 */


void hfp_create_sdp_record(uint8_t * service, uint16_t service_uuid, int rfcomm_channel_nr, const char * name, uint16_t supported_features){
    uint8_t* attribute;
    de_create_sequence(service);

    // 0x0000 "Service Record Handle"
    de_add_number(service, DE_UINT, DE_SIZE_16, SDP_ServiceRecordHandle);
    de_add_number(service, DE_UINT, DE_SIZE_32, 0x10001);

    // 0x0001 "Service Class ID List"
    de_add_number(service,  DE_UINT, DE_SIZE_16, SDP_ServiceClassIDList);
    attribute = de_push_sequence(service);
    {
        //  "UUID for Service"
        de_add_number(attribute, DE_UUID, DE_SIZE_16, service_uuid);
        de_add_number(attribute, DE_UUID, DE_SIZE_16, SDP_GenericAudio);
    }
    de_pop_sequence(service, attribute);

    // 0x0004 "Protocol Descriptor List"
    de_add_number(service,  DE_UINT, DE_SIZE_16, SDP_ProtocolDescriptorList);
    attribute = de_push_sequence(service);
    {
        uint8_t* l2cpProtocol = de_push_sequence(attribute);
        {
            de_add_number(l2cpProtocol,  DE_UUID, DE_SIZE_16, SDP_L2CAPProtocol);
        }
        de_pop_sequence(attribute, l2cpProtocol);
        
        uint8_t* rfcomm = de_push_sequence(attribute);
        {
            de_add_number(rfcomm,  DE_UUID, DE_SIZE_16, SDP_RFCOMMProtocol);  // rfcomm_service
            de_add_number(rfcomm,  DE_UINT, DE_SIZE_8,  rfcomm_channel_nr);  // rfcomm channel
        }
        de_pop_sequence(attribute, rfcomm);
    }
    de_pop_sequence(service, attribute);

    
    // 0x0005 "Public Browse Group"
    de_add_number(service,  DE_UINT, DE_SIZE_16, SDP_BrowseGroupList); // public browse group
    attribute = de_push_sequence(service);
    {
        de_add_number(attribute,  DE_UUID, DE_SIZE_16, SDP_PublicBrowseGroup);
    }
    de_pop_sequence(service, attribute);

    // 0x0009 "Bluetooth Profile Descriptor List"
    de_add_number(service,  DE_UINT, DE_SIZE_16, SDP_BluetoothProfileDescriptorList);
    attribute = de_push_sequence(service);
    {
        uint8_t *sppProfile = de_push_sequence(attribute);
        {
            de_add_number(sppProfile,  DE_UUID, DE_SIZE_16, SDP_Handsfree); 
            de_add_number(sppProfile,  DE_UINT, DE_SIZE_16, 0x0107); // Verision 1.7
        }
        de_pop_sequence(attribute, sppProfile);
    }
    de_pop_sequence(service, attribute);

    // 0x0100 "Service Name"
    de_add_number(service,  DE_UINT, DE_SIZE_16, 0x0100);
    de_add_data(service,  DE_STRING, strlen(name), (uint8_t *) name);
    
    de_add_number(service, DE_UINT, DE_SIZE_16, supported_features);
}

static hfp_connection_t * connection_doing_sdp_query = NULL;
static void handle_query_rfcomm_event(sdp_query_event_t * event, void * context){
    sdp_query_rfcomm_service_event_t * ve;
    sdp_query_complete_event_t * ce;
    hfp_connection_t * connection = connection_doing_sdp_query;
    
    if ( connection->state != HFP_W4_SDP_QUERY_COMPLETE) return;
    
    switch (event->type){
        case SDP_QUERY_RFCOMM_SERVICE:
            ve = (sdp_query_rfcomm_service_event_t*) event;
            if (!connection) {
                log_error("handle_query_rfcomm_event alloc connection for RFCOMM port %u failed", ve->channel_nr);
                return;
            }
            connection->rfcomm_channel_nr = ve->channel_nr;
            break;
        case SDP_QUERY_COMPLETE:
            connection_doing_sdp_query = NULL;
            ce = (sdp_query_complete_event_t*) event;
            
            if (connection->rfcomm_channel_nr > 0){
                connection->state = HFP_W4_RFCOMM_CONNECTED;
                log_info("HFP: SDP_QUERY_COMPLETE context %p, addr %s, state %d", connection, bd_addr_to_str( connection->remote_addr),  connection->state);
                rfcomm_create_channel_internal(NULL, connection->remote_addr, connection->rfcomm_channel_nr); 
                break;
            }
            log_info("rfcomm service not found, status %u.", ce->status);
            break;
        default:
            break;
    }
}

void hfp_handle_hci_event(hfp_callback_t callback, uint8_t packet_type, uint8_t *packet, uint16_t size){
    bd_addr_t event_addr;
    uint16_t rfcomm_cid, handle;
    hfp_connection_t * context = NULL;

    switch (packet[0]) {
        case BTSTACK_EVENT_STATE:
            // bt stack activated, get started 
            if (packet[2] == HCI_STATE_WORKING){
                printf("BTstack activated, get started .\n");
            }
            break;

        case HCI_EVENT_PIN_CODE_REQUEST:
            // inform about pin code request
            printf("Pin code request - using '0000'\n\r");
            bt_flip_addr(event_addr, &packet[2]);
            hci_send_cmd(&hci_pin_code_request_reply, &event_addr, 4, "0000");
            break;
        
        case RFCOMM_EVENT_INCOMING_CONNECTION:
            // data: event (8), len(8), address(48), channel (8), rfcomm_cid (16)
            bt_flip_addr(event_addr, &packet[2]); 
            context = get_hfp_connection_context_for_bd_addr(event_addr);
            
            if (!context || context->state != HFP_IDLE) return;

            context->rfcomm_cid = READ_BT_16(packet, 9);
            context->state = HFP_W4_RFCOMM_CONNECTED;
            printf("RFCOMM channel %u requested for %s\n", context->rfcomm_cid, bd_addr_to_str(context->remote_addr));
            rfcomm_accept_connection_internal(context->rfcomm_cid);
            break;

        case RFCOMM_EVENT_OPEN_CHANNEL_COMPLETE:
            // data: event(8), len(8), status (8), address (48), handle(16), server channel(8), rfcomm_cid(16), max frame size(16)
            printf("RFCOMM_EVENT_OPEN_CHANNEL_COMPLETE packet_handler type %u, packet[0] %x\n", packet_type, packet[0]);
            bt_flip_addr(event_addr, &packet[3]); 
            context = get_hfp_connection_context_for_bd_addr(event_addr);
            if (!context || context->state != HFP_W4_RFCOMM_CONNECTED) return;
            
            if (packet[2]) {
                hfp_emit_event(callback, HFP_SUBEVENT_SERVICE_LEVEL_CONNECTION_ESTABLISHED, packet[2]);
                remove_hfp_connection_context(context);
            } else {
                context->con_handle = READ_BT_16(packet, 9);
                context->rfcomm_cid = READ_BT_16(packet, 12);
                uint16_t mtu = READ_BT_16(packet, 14);
                printf("RFCOMM channel open succeeded. Context %p, RFCOMM Channel ID 0x%02x, max frame size %u\n", context, context->rfcomm_cid, mtu);
                        
                switch (context->state){
                    case HFP_W4_RFCOMM_CONNECTED:
                        context->state = HFP_EXCHANGE_SUPPORTED_FEATURES;
                        break;
                    case HFP_W4_CONNECTION_ESTABLISHED_TO_SHUTDOWN:
                        context->state = HFP_W2_DISCONNECT_RFCOMM;
                        printf("Shutting down RFCOMM.\n");
                        break;
                    default:
                        break;
                }
            }
            break;
        
        case HCI_EVENT_SYNCHRONOUS_CONNECTION_COMPLETE:{
            int index = 2;
            uint8_t status = packet[index++];
            uint16_t sco_handle = READ_BT_16(packet, index);
            index+=2;
            bd_addr_t address; 
            memcpy(address, &packet[index], 6);
            index+=6;
            uint8_t link_type = packet[index++];
            uint8_t transmission_interval = packet[index++];  // measured in slots
            uint8_t retransmission_interval = packet[index++];// measured in slots
            uint16_t rx_packet_length = READ_BT_16(packet, index); // measured in bytes
            index+=2;
            uint16_t tx_packet_length = READ_BT_16(packet, index); // measured in bytes
            index+=2;
            uint8_t air_mode = packet[index];

            if (status != 0){
                log_error("(e)SCO Connection is not established, status %u", status);
                break;
            }
            switch (link_type){
                case 0x00:
                    printf("SCO Connection established. \n");
                    if (transmission_interval != 0) log_error("SCO Connection: transmission_interval not zero: %d.", transmission_interval);
                    if (retransmission_interval != 0) log_error("SCO Connection: retransmission_interval not zero: %d.", retransmission_interval);
                    if (rx_packet_length != 0) log_error("SCO Connection: rx_packet_length not zero: %d.", rx_packet_length);
                    if (tx_packet_length != 0) log_error("SCO Connection: tx_packet_length not zero: %d.", tx_packet_length);
                    break;
                case 0x02:
                    printf("eSCO Connection established. \n");
                    break;
                default:
                    log_error("(e)SCO reserved link_type 0x%2x", link_type);
                    break;
            }
            log_info("sco_handle 0x%2x, address %s, transmission_interval %u slots, retransmission_interval %u slots, " 
                 " rx_packet_length %u bytes, tx_packet_length %u bytes, air_mode 0x%2x (0x02 == CVSD)", sco_handle,
                 bd_addr_to_str(address), transmission_interval, retransmission_interval, rx_packet_length, tx_packet_length, air_mode);

            context = get_hfp_connection_context_for_bd_addr(address);

            if (context->state == HFP_W4_CONNECTION_ESTABLISHED_TO_SHUTDOWN){
                context->state = HFP_W2_DISCONNECT_SCO;
                break;
            }
            
            context->sco_handle = sco_handle;
            context->state = HFP_AUDIO_CONNECTION_ESTABLISHED;
            hfp_emit_event(callback, HFP_SUBEVENT_AUDIO_CONNECTION_COMPLETE, packet[2]);
            break;                
        }

        case RFCOMM_EVENT_CHANNEL_CLOSED:
            rfcomm_cid = READ_BT_16(packet,2);
            context = get_hfp_connection_context_for_rfcomm_cid(rfcomm_cid);
            if (!context) break;
            if (context->state == HFP_W4_RFCOMM_DISCONNECTED_AND_RESTART){
                context->state = HFP_IDLE;
                hfp_establish_service_level_connection(context->remote_addr, context->service_uuid);
                break;
            }
            
            remove_hfp_connection_context(context);
            hfp_emit_event(callback, HFP_SUBEVENT_SERVICE_LEVEL_CONNECTION_RELEASED, 0);
            break;

        case HCI_EVENT_DISCONNECTION_COMPLETE:
            handle = READ_BT_16(packet,3);
            context = get_hfp_connection_context_for_handle(handle);
            if (!context) break;
            if (context->state == HFP_W4_RFCOMM_DISCONNECTED_AND_RESTART){
                context->state = HFP_IDLE;
                hfp_establish_service_level_connection(context->remote_addr, context->service_uuid);
                break;
            }
            hfp_emit_event(callback, HFP_SUBEVENT_SERVICE_LEVEL_CONNECTION_RELEASED, packet[2]);
            remove_hfp_connection_context(context);
            break;

        default:
            break;
    }
}

// translates command string into hfp_command_t CMD and flags to distinguish between CMD=, CMD?, CMD=?
void process_command(hfp_connection_t * context){
    if (context->line_size < 2) return;
    // printf("process_command %s\n", context->line_buffer);
    context->command = HFP_CMD_NONE;
    int offset = 0;
    int isHandsFree = 1;

    if (strncmp((char *)context->line_buffer, "AT", 2) == 0){
        offset = 2;
        isHandsFree = 0;
    }
    
    if (strncmp((char *)context->line_buffer+offset, HFP_ERROR, strlen(HFP_ERROR)) == 0){
        context->command = HFP_CMD_ERROR;
        return;
    }

    if (isHandsFree && strncmp((char *)context->line_buffer+offset, HFP_OK, strlen(HFP_OK)) == 0){
        //printf("parsed HFP_CMD_OK \n");
        context->command = HFP_CMD_OK;
        return;
    }

    if (strncmp((char *)context->line_buffer+offset, HFP_SUPPORTED_FEATURES, strlen(HFP_SUPPORTED_FEATURES)) == 0){
        context->command = HFP_CMD_SUPPORTED_FEATURES;
        return;
    }

    if (strncmp((char *)context->line_buffer+offset, HFP_INDICATOR, strlen(HFP_INDICATOR)) == 0){
        //printf("parsed HFP_INDICATOR \n");
        context->command = HFP_CMD_INDICATOR;
        if (isHandsFree) return;
        
        if (strncmp((char *)context->line_buffer+strlen(HFP_INDICATOR)+offset, "?", 1) == 0){
            context->retrieve_ag_indicators_status = 1; 
            context->retrieve_ag_indicators = 0;     
        } else {
            context->retrieve_ag_indicators = 1; 
            context->retrieve_ag_indicators_status = 0;    
        }
        return;
    }

    if (strncmp((char *)context->line_buffer+offset, HFP_AVAILABLE_CODECS, strlen(HFP_AVAILABLE_CODECS)) == 0){
        context->command = HFP_CMD_AVAILABLE_CODECS;
        return;
    }

    if (strncmp((char *)context->line_buffer+offset, HFP_ENABLE_STATUS_UPDATE_FOR_AG_INDICATORS, strlen(HFP_ENABLE_STATUS_UPDATE_FOR_AG_INDICATORS)) == 0){
        context->command = HFP_CMD_ENABLE_INDICATOR_STATUS_UPDATE;
        return;
    }

    if (strncmp((char *)context->line_buffer+offset, HFP_SUPPORT_CALL_HOLD_AND_MULTIPARTY_SERVICES, strlen(HFP_SUPPORT_CALL_HOLD_AND_MULTIPARTY_SERVICES)) == 0){
        context->command = HFP_CMD_SUPPORT_CALL_HOLD_AND_MULTIPARTY_SERVICES;
        return;
    } 

    if (strncmp((char *)context->line_buffer+offset, HFP_GENERIC_STATUS_INDICATOR, strlen(HFP_GENERIC_STATUS_INDICATOR)) == 0){
        context->command = HFP_CMD_GENERIC_STATUS_INDICATOR;
        if (isHandsFree) return;

        if (strncmp((char *)context->line_buffer+strlen(HFP_GENERIC_STATUS_INDICATOR)+offset, "=?", 2) == 0){
            context->list_generic_status_indicators = 0;
            context->retrieve_generic_status_indicators = 1;
            context->retrieve_generic_status_indicators_state = 0;    
        } else if (strncmp((char *)context->line_buffer+strlen(HFP_GENERIC_STATUS_INDICATOR)+offset, "=", 1) == 0){
            context->list_generic_status_indicators = 1;
            context->retrieve_generic_status_indicators = 0;
            context->retrieve_generic_status_indicators_state = 0;    
        } else {
            context->list_generic_status_indicators = 0;
            context->retrieve_generic_status_indicators = 0;
            context->retrieve_generic_status_indicators_state = 1;
        }
        return;
    } 

    if (strncmp((char *)context->line_buffer+offset, HFP_UPDATE_ENABLE_STATUS_FOR_INDIVIDUAL_AG_INDICATORS, strlen(HFP_UPDATE_ENABLE_STATUS_FOR_INDIVIDUAL_AG_INDICATORS)) == 0){
        context->command = HFP_CMD_ENABLE_INDIVIDUAL_AG_INDICATOR_STATUS_UPDATE;
        return;
    } 
    

    if (strncmp((char *)context->line_buffer+offset, HFP_QUERY_OPERATOR_SELECTION, strlen(HFP_QUERY_OPERATOR_SELECTION)) == 0){
        context->command = HFP_CMD_QUERY_OPERATOR_SELECTION;
        context->operator_name = 1;
        context->operator_name_format = 0;
        if (isHandsFree) return;

        context->operator_name = 0;
        if (strncmp((char *)context->line_buffer+strlen(HFP_QUERY_OPERATOR_SELECTION)+offset, "=", 1) == 0){
            context->operator_name_format = 1; 
        } 
        return;
    }

    if (strncmp((char *)context->line_buffer+offset, HFP_TRANSFER_AG_INDICATOR_STATUS, strlen(HFP_TRANSFER_AG_INDICATOR_STATUS)) == 0){
        context->command = HFP_CMD_TRANSFER_AG_INDICATOR_STATUS;
        return;
    } 

    if (isHandsFree && strncmp((char *)context->line_buffer+offset, HFP_EXTENDED_AUDIO_GATEWAY_ERROR, strlen(HFP_EXTENDED_AUDIO_GATEWAY_ERROR)) == 0){
        printf(" process command 1 %s \n", context->line_buffer);
        context->command = HFP_CMD_EXTENDED_AUDIO_GATEWAY_ERROR;
        return;
    }

    if (!isHandsFree && strncmp((char *)context->line_buffer+offset, HFP_ENABLE_EXTENDED_AUDIO_GATEWAY_ERROR, strlen(HFP_ENABLE_EXTENDED_AUDIO_GATEWAY_ERROR)) == 0){
        printf(" process command 2 %s \n", context->line_buffer);
        context->command = HFP_CMD_ENABLE_EXTENDED_AUDIO_GATEWAY_ERROR;
        return;
    }

    if (strncmp((char *)context->line_buffer+offset, HFP_TRIGGER_CODEC_CONNECTION_SETUP, strlen(HFP_TRIGGER_CODEC_CONNECTION_SETUP)) == 0){
        context->command = HFP_CMD_TRIGGER_CODEC_CONNECTION_SETUP;
        context->trigger_codec_connection_setup = 1;
        return;
    } 

    if (strncmp((char *)context->line_buffer+offset, HFP_CONFIRM_COMMON_CODEC, strlen(HFP_CONFIRM_COMMON_CODEC)) == 0){
        context->command = HFP_CMD_CONFIRM_COMMON_CODEC;

        return;
    } 

    printf(" process unknown command 3 %s \n", context->line_buffer);
}

uint32_t fromBinary(char *s) {
    return (uint32_t) strtol(s, NULL, 2);
}

static void hfp_parser_store_byte(hfp_connection_t * context, uint8_t byte){
    // TODO: add limit
    context->line_buffer[context->line_size++] = byte;
    context->line_buffer[context->line_size] = 0;
}
static int hfp_parser_is_buffer_empty(hfp_connection_t * context){
    return context->line_size == 0;
}

static int hfp_parser_is_end_of_line(uint8_t byte){
    return byte == '\n' || byte == '\r';
}

static int hfp_parser_is_end_of_header(uint8_t byte){
    return hfp_parser_is_end_of_line(byte) || byte == ':' || byte == '?';
}

static int hfp_parser_found_separator(hfp_connection_t * context, uint8_t byte){
    if (context->keep_separator == 1) return 1;

    int found_separator =   byte == ',' || byte == '\n'|| byte == '\r'||
                            byte == ')' || byte == '(' || byte == ':' || 
                            byte == '-' || byte == '"' ||  byte == '?'|| byte == '=';
    return found_separator;
}

static void hfp_parser_next_state(hfp_connection_t * context, uint8_t byte){
    context->line_size = 0;
    if (hfp_parser_is_end_of_line(byte)){
        context->parser_item_index = 0;
        context->parser_state = HFP_PARSER_CMD_HEADER;
        return;
    }
    switch (context->parser_state){
        case HFP_PARSER_CMD_HEADER:
            context->parser_state = HFP_PARSER_CMD_SEQUENCE;
            if (context->keep_separator == 1){
                hfp_parser_store_byte(context, byte);
                context->keep_separator = 0;
            }
            break;
        case HFP_PARSER_CMD_SEQUENCE:
            switch (context->command){
                case HFP_CMD_TRANSFER_AG_INDICATOR_STATUS:
                case HFP_CMD_QUERY_OPERATOR_SELECTION:
                    context->parser_state = HFP_PARSER_SECOND_ITEM;
                    break;
                case HFP_CMD_INDICATOR:
                    if (context->retrieve_ag_indicators == 1){
                        context->parser_state = HFP_PARSER_SECOND_ITEM;
                        break;
                    }
                    break;
                case HFP_CMD_GENERIC_STATUS_INDICATOR:
                    if (context->retrieve_generic_status_indicators_state == 1){
                        context->parser_state = HFP_PARSER_SECOND_ITEM;
                        break;
                    }
                    break;
                default:
                    break;
            }
            break;
        case HFP_PARSER_SECOND_ITEM:
            context->parser_state = HFP_PARSER_THIRD_ITEM;
            break;
        case HFP_PARSER_THIRD_ITEM:
            if (context->command == HFP_CMD_INDICATOR && context->retrieve_ag_indicators){
                context->parser_state = HFP_PARSER_CMD_SEQUENCE;
                break;
            }
            context->parser_state = HFP_PARSER_CMD_HEADER;
            break;
    }
}

void hfp_parse(hfp_connection_t * context, uint8_t byte){
    int value;
    
    // TODO: handle space inside word        
    if (byte == ' ' && context->parser_state > HFP_PARSER_CMD_HEADER) return;

    if (!hfp_parser_found_separator(context, byte)){
        hfp_parser_store_byte(context, byte);
        return;
    }
    if (hfp_parser_is_end_of_line(byte)) {
        if (hfp_parser_is_buffer_empty(context)){
            context->parser_state = HFP_PARSER_CMD_HEADER;
        }
    }
    if (hfp_parser_is_buffer_empty(context)) return;


    switch (context->parser_state){
        case HFP_PARSER_CMD_HEADER: // header
            // printf(" parse header 1 \n");
            if (byte == '='){
                context->keep_separator = 1;
                hfp_parser_store_byte(context, byte);
                return;
            }
            
            if (byte == '?'){
                context->keep_separator = 0;
                hfp_parser_store_byte(context, byte);
                return;
            }
            // printf(" parse header 2 %s, keep separator $ %d\n", context->line_buffer, context->keep_separator);
            if (hfp_parser_is_end_of_header(byte) || context->keep_separator == 1){
                // printf(" parse header 3 %s, keep separator $ %d\n", context->line_buffer, context->keep_separator);
                process_command(context);
            }
            break;

        case HFP_PARSER_CMD_SEQUENCE: // parse comma separated sequence, ignore breacktes
            switch (context->command){
                case HFP_CMD_CONFIRM_COMMON_CODEC:
                    context->remote_codec_received = atoi((char*)context->line_buffer);
                    break;
                case HFP_CMD_SUPPORTED_FEATURES:
                    context->remote_supported_features = atoi((char*)context->line_buffer);
                    printf("Parsed supported feature %d\n", context->remote_supported_features);
                    break;
                case HFP_CMD_AVAILABLE_CODECS:
                    printf("Parsed codec %s\n", context->line_buffer);
                    context->remote_codecs[context->parser_item_index] = (uint16_t)atoi((char*)context->line_buffer);
                    context->parser_item_index++;
                    context->remote_codecs_nr = context->parser_item_index;
                    break;
                case HFP_CMD_INDICATOR:
                    if (context->retrieve_ag_indicators == 1){
                        strcpy((char *)context->ag_indicators[context->parser_item_index].name,  (char *)context->line_buffer);
                        context->ag_indicators[context->parser_item_index].index = context->parser_item_index+1;
                        printf("Indicator %d: %s (", context->ag_indicators_nr+1, context->line_buffer);
                    }

                    if (context->retrieve_ag_indicators_status == 1){ 
                        printf("Parsed Indicator %d with status: %s\n", context->parser_item_index+1, context->line_buffer);
                        context->ag_indicators[context->parser_item_index].status = atoi((char *) context->line_buffer);
                        context->parser_item_index++;
                        break;
                    }
                    break;
                case HFP_CMD_ENABLE_INDICATOR_STATUS_UPDATE:
                    context->parser_item_index++;
                    if (context->parser_item_index != 4) break;
                    printf("Parsed Enable indicators: %s\n", context->line_buffer);
                    value = atoi((char *)&context->line_buffer[0]);
                    context->enable_status_update_for_ag_indicators = (uint8_t) value;
                    break;
                case HFP_CMD_SUPPORT_CALL_HOLD_AND_MULTIPARTY_SERVICES:
                    printf("Parsed Support call hold: %s\n", context->line_buffer);
                    if (context->line_size > 2 ) break;
                    strcpy((char *)context->remote_call_services[context->remote_call_services_nr].name,  (char *)context->line_buffer);
                    context->remote_call_services_nr++;
                    break;
                case HFP_CMD_GENERIC_STATUS_INDICATOR:
                    printf("parser HFP_CMD_GENERIC_STATUS_INDICATOR 1 (%d, %d, %d)\n", 
                            context->list_generic_status_indicators, 
                            context->retrieve_generic_status_indicators,
                            context->retrieve_generic_status_indicators_state);
                    if (context->retrieve_generic_status_indicators == 1 || context->list_generic_status_indicators == 1){
                        printf("Parsed Generic status indicator: %s\n", context->line_buffer);
                        context->generic_status_indicators[context->parser_item_index].uuid = (uint16_t)atoi((char*)context->line_buffer);
                        context->parser_item_index++;
                        context->generic_status_indicators_nr = context->parser_item_index;
                        break;    
                    }
                    printf("parser HFP_CMD_GENERIC_STATUS_INDICATOR 2\n");
                    if (context->retrieve_generic_status_indicators_state == 1){
                        // HF parses inital AG gen. ind. state
                        printf("Parsed List generic status indicator %s state: ", context->line_buffer);
                        context->parser_item_index = (uint8_t)atoi((char*)context->line_buffer);
                        break;
                    }
                    break;
    
                case HFP_CMD_ENABLE_INDIVIDUAL_AG_INDICATOR_STATUS_UPDATE:
                    // AG parses new gen. ind. state
                    printf("Parsed Enable ag indicator state: %s\n", context->line_buffer);
                    value = atoi((char *)&context->line_buffer[0]);
                    if (!context->ag_indicators[context->parser_item_index].mandatory){
                        context->ag_indicators[context->parser_item_index].enabled = value;
                    }
                    context->parser_item_index++;
                    break;
                case HFP_CMD_TRANSFER_AG_INDICATOR_STATUS:
                    // indicators are indexed starting with 1
                    context->parser_item_index = atoi((char *)&context->line_buffer[0]) - 1;
                    printf("Parsed status of the AG indicator %d, status ", context->parser_item_index);
                    break;
                case HFP_CMD_QUERY_OPERATOR_SELECTION:
                    if (context->operator_name_format == 1){
                        if (context->line_buffer[0] == '3'){
                            printf("Parsed Set network operator format : %s, ", context->line_buffer);
                            break;
                        }
                        // TODO emit ERROR, wrong format
                        printf("ERROR Set network operator format: index %s not supported\n", context->line_buffer);
                        break;
                    }

                    if (context->operator_name == 1) {
                        context->network_operator.mode = atoi((char *)&context->line_buffer[0]);
                        printf("Parsed network operator mode: %d, ", context->network_operator.mode);
                        break;
                    }
                    break;
                case HFP_CMD_ERROR:
                    break;
                case HFP_CMD_EXTENDED_AUDIO_GATEWAY_ERROR:
                    context->extended_audio_gateway_error = (uint8_t)atoi((char*)context->line_buffer);
                    break;
                case HFP_CMD_ENABLE_EXTENDED_AUDIO_GATEWAY_ERROR:
                    context->enable_extended_audio_gateway_error_report = (uint8_t)atoi((char*)context->line_buffer);
                    context->send_ok = 1;
                    context->extended_audio_gateway_error = 0;
                    break;
                default:
                    break;
            }
            break;

        case HFP_PARSER_SECOND_ITEM:
            switch (context->command){
                case HFP_CMD_QUERY_OPERATOR_SELECTION:
                    if (context->operator_name_format == 1) {
                        printf("format %s \n", context->line_buffer);
                        context->network_operator.format =  atoi((char *)&context->line_buffer[0]);
                        break;
                    }
                    if (context->operator_name == 1){
                        printf("format %s, ", context->line_buffer);
                        context->network_operator.format =  atoi((char *)&context->line_buffer[0]);
                    }
                    break;
                case HFP_CMD_GENERIC_STATUS_INDICATOR:
                    context->generic_status_indicators[context->parser_item_index].state = (uint8_t)atoi((char*)context->line_buffer);
                    break;
                case HFP_CMD_TRANSFER_AG_INDICATOR_STATUS:
                    context->ag_indicators[context->parser_item_index].status = (uint8_t)atoi((char*)context->line_buffer);
                    context->ag_indicators[context->parser_item_index].status_changed = 1;
                    break;
                case HFP_CMD_INDICATOR:
                    if (context->retrieve_ag_indicators == 1){
                        context->ag_indicators[context->parser_item_index].min_range = atoi((char *)context->line_buffer);
                        printf("%s, ", context->line_buffer);
                    }
                    break;
                default:
                    break;
            }
            break;

        case HFP_PARSER_THIRD_ITEM:
             switch (context->command){
                case HFP_CMD_QUERY_OPERATOR_SELECTION:
                    if (context->operator_name == 1){
                        strcpy(context->network_operator.name, (char *)context->line_buffer);
                        printf("name %s\n", context->line_buffer);
                    }
                    break;
                case HFP_CMD_INDICATOR:
                    if (context->retrieve_ag_indicators == 1){
                        context->ag_indicators[context->parser_item_index].max_range = atoi((char *)context->line_buffer);
                        context->parser_item_index++;
                        context->ag_indicators_nr = context->parser_item_index;
                        printf("%s)\n", context->line_buffer);
                    }
                    break;
                default:
                    break;
            }
            break;
    }
    hfp_parser_next_state(context, byte);
}

void hfp_init(uint16_t rfcomm_channel_nr){
    rfcomm_register_service_internal(NULL, rfcomm_channel_nr, 0xffff);  
    sdp_query_rfcomm_register_callback(handle_query_rfcomm_event, NULL);
}

void hfp_establish_service_level_connection(bd_addr_t bd_addr, uint16_t service_uuid){
    hfp_connection_t * context = provide_hfp_connection_context_for_bd_addr(bd_addr);
    log_info("hfp_connect %s, context %p", bd_addr_to_str(bd_addr), context);
    
    if (!context) {
        log_error("hfp_establish_service_level_connection for addr %s failed", bd_addr_to_str(bd_addr));
        return;
    }
    switch (context->state){
        case HFP_W2_DISCONNECT_RFCOMM:
            context->state = HFP_SERVICE_LEVEL_CONNECTION_ESTABLISHED;
            return;
        case HFP_W4_RFCOMM_DISCONNECTED:
            context->state = HFP_W4_RFCOMM_DISCONNECTED_AND_RESTART;
            return;
        case HFP_IDLE:
            memcpy(context->remote_addr, bd_addr, 6);
            context->state = HFP_W4_SDP_QUERY_COMPLETE;
            connection_doing_sdp_query = context;
            context->service_uuid = service_uuid;
            sdp_query_rfcomm_channel_and_name_for_uuid(context->remote_addr, service_uuid);
            break;
        default:
            break;
    }
}

void hfp_release_service_level_connection(hfp_connection_t * context){
    if (!context) return;
            
    switch (context->state){
        case HFP_SERVICE_LEVEL_CONNECTION_ESTABLISHED:
            context->state = HFP_W2_DISCONNECT_RFCOMM;
            break;
        case HFP_W4_RFCOMM_CONNECTED:
            context->state = HFP_W4_CONNECTION_ESTABLISHED_TO_SHUTDOWN;
            break;
        default:
            break;
    }
    return;
}

void hfp_establish_audio_connection(hfp_connection_t * context, uint8_t codec_negotiation_feature_enabled){
}

void hfp_release_audio_connection(hfp_connection_t * context){    
}

