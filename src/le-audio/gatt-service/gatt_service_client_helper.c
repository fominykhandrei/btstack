/*
 * Copyright (C) 2023 BlueKitchen GmbH
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

#define BTSTACK_FILE__ "gatt_service_client_helper.c"

#include "btstack_config.h"

#ifdef ENABLE_TESTING_SUPPORT
#include <stdio.h>
#include <unistd.h>
#endif

#include <stdint.h>
#include <string.h>


#include "le-audio/gatt-service/gatt_service_client_helper.h"

#include "btstack_memory.h"
#include "ble/core.h"
#include "ble/gatt_client.h"
#include "bluetooth_gatt.h"
#include "btstack_debug.h"
#include "btstack_event.h"
#include "btstack_run_loop.h"
#include "gap.h"

// active gatt client query
static gatt_service_client_helper_t * gatt_service_active_client;

static void gatt_service_client_handle_gatt_client_event(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size);

// LE Audio Service Client helper functions
static void gatt_service_client_finalize_connection(gatt_service_client_helper_t * client, gatt_service_client_connection_helper_t * connection){
    if (client == NULL){
        return;
    }
    btstack_linked_list_remove(&client->connections, (btstack_linked_item_t*) connection);
    gatt_service_active_client = NULL;
}

static gatt_service_client_connection_helper_t * gatt_service_client_get_connection_for_con_handle(gatt_service_client_helper_t * client, hci_con_handle_t con_handle){
    btstack_linked_list_iterator_t it;    
    btstack_linked_list_iterator_init(&it, (btstack_linked_list_t *) &client->connections);
    while (btstack_linked_list_iterator_has_next(&it)){
        gatt_service_client_connection_helper_t * connection = (gatt_service_client_connection_helper_t *)btstack_linked_list_iterator_next(&it);
        if (connection->con_handle != con_handle) continue;
        return connection;
    }
    return NULL;
}

gatt_service_client_connection_helper_t * gatt_service_client_get_connection_for_cid(gatt_service_client_helper_t * client, uint16_t connection_cid){
    btstack_linked_list_iterator_t it;    
    btstack_linked_list_iterator_init(&it, (btstack_linked_list_t *) &client->connections);
    while (btstack_linked_list_iterator_has_next(&it)){
        gatt_service_client_connection_helper_t * connection = (gatt_service_client_connection_helper_t *)btstack_linked_list_iterator_next(&it);
        if (connection->cid != connection_cid) continue;
        return connection;
    }
    return NULL;
}

static void gatt_service_client_emit_connected(btstack_packet_handler_t event_callback, hci_con_handle_t con_handle, uint16_t cid, uint8_t status){
    btstack_assert(event_callback != NULL);

    uint8_t event[8];
    int pos = 0;
    event[pos++] = HCI_EVENT_GATTSERVICE_META;
    event[pos++] = sizeof(event) - 2;
    event[pos++] = GATTSERVICE_SUBEVENT_CLIENT_CONNECTED;
    little_endian_store_16(event, pos, con_handle);
    pos += 2;
    little_endian_store_16(event, pos, cid);
    pos += 2;
    event[pos++] = status;
    (*event_callback)(HCI_EVENT_PACKET, 0, event, sizeof(event));
}

static void gatt_service_client_emit_disconnected(btstack_packet_handler_t event_callback, hci_con_handle_t con_handle, uint16_t cid){
    btstack_assert(event_callback != NULL);

    uint8_t event[7];
    int pos = 0;
    event[pos++] = HCI_EVENT_GATTSERVICE_META;
    event[pos++] = sizeof(event) - 2;
    event[pos++] = GATTSERVICE_SUBEVENT_CLIENT_DISCONNECTED;
    little_endian_store_16(event, pos, con_handle);
    pos += 2;
    little_endian_store_16(event, pos, cid);
    pos += 2;
    (*event_callback)(HCI_EVENT_PACKET, 0, event, sizeof(event));
}

static uint16_t gatt_service_client_get_next_cid(gatt_service_client_helper_t * client){
    client->cid_counter = btstack_next_cid_ignoring_zero(client->cid_counter);
    return client->cid_counter;
}

void gatt_service_client_hci_event_handler(gatt_service_client_helper_t * client, uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size){
    UNUSED(channel);
    UNUSED(size);

    if (packet_type != HCI_EVENT_PACKET) return;
    
    hci_con_handle_t con_handle;
    gatt_service_client_connection_helper_t * connection;
    
    switch (hci_event_packet_get_type(packet)){
        case HCI_EVENT_DISCONNECTION_COMPLETE:
            con_handle = hci_event_disconnection_complete_get_connection_handle(packet);
            connection = gatt_service_client_get_connection_for_con_handle(client, con_handle);
            if (connection != NULL){
                gatt_service_client_emit_disconnected(client->packet_handler, connection->con_handle, connection->cid);
                gatt_service_client_finalize_connection(client, connection);
            }
            break;
        default:
            break;
    }
}

static bool gatt_service_client_next_index_for_descriptor_query(gatt_service_client_connection_helper_t * connection) {
    bool next_query_found = false;
    while (!next_query_found && (connection->characteristic_index < connection->characteristics_num)) {
        if ((connection->characteristics[connection->characteristic_index].properties & ATT_PROPERTY_NOTIFY) != 0u){
            next_query_found = true;
            break;
        }
        connection->characteristic_index++;
    }
    return next_query_found;
}

static bool gatt_service_client_next_index_for_notification_query(gatt_service_client_connection_helper_t * connection) {
    bool next_query_found = false;
    while (!next_query_found && (connection->characteristic_index < connection->characteristics_num)) {
        if (connection->characteristics[connection->characteristic_index].client_configuration_handle != 0) {
            next_query_found = true;
            break;
        }
        connection->characteristic_index++;
    }
    return next_query_found;
}

static uint8_t gatt_service_client_register_notification(gatt_service_client_connection_helper_t * connection){
    gatt_client_characteristic_t characteristic;
    uint8_t status = ERROR_CODE_UNSUPPORTED_FEATURE_OR_PARAMETER_VALUE;
    
    if (connection->characteristics[connection->characteristic_index].client_configuration_handle != 0){
        characteristic.value_handle = connection->characteristics[connection->characteristic_index].value_handle;
        characteristic.end_handle = connection->characteristics[connection->characteristic_index].end_handle;
        characteristic.properties = connection->characteristics[connection->characteristic_index].properties;

        status = gatt_client_write_client_characteristic_configuration(
                    &gatt_service_client_handle_gatt_client_event, 
                    connection->con_handle, 
                    &characteristic, 
                    GATT_CLIENT_CHARACTERISTICS_CONFIGURATION_NOTIFICATION);
      
        // notification supported, register for value updates
        if (status == ERROR_CODE_SUCCESS){
            gatt_client_listen_for_characteristic_value_updates(
                &connection->characteristics[connection->characteristic_index].notification_listener, 
                connection->handle_gatt_server_notification,
                connection->con_handle, &characteristic);
        }
    }
    return status;
}


static void gatt_service_client_run_for_client(gatt_service_client_helper_t * client, gatt_service_client_connection_helper_t * connection){
    uint8_t status = ATT_ERROR_SUCCESS;
    gatt_client_service_t service;
    gatt_client_characteristic_t characteristic;

    switch (connection->state){
        case GATT_SERVICE_CLIENT_STATE_W2_QUERY_SERVICE:
            connection->state = GATT_SERVICE_CLIENT_STATE_W4_SERVICE_RESULT;
            status = gatt_client_discover_primary_services_by_uuid16(
                &gatt_service_client_handle_gatt_client_event, 
                connection->con_handle, 
                client->service_uuid16);
            break;

        case GATT_SERVICE_CLIENT_STATE_W2_QUERY_CHARACTERISTICS:
#ifdef ENABLE_TESTING_SUPPORT
            printf("Read characteristics [service 0x%04x]:\n", client->service_uuid16);
#endif
            connection->state = GATT_SERVICE_CLIENT_STATE_W4_CHARACTERISTIC_RESULT;

            service.start_group_handle = connection->start_handle;
            service.end_group_handle = connection->end_handle;
            service.uuid16 = client->service_uuid16;

            status = gatt_client_discover_characteristics_for_service(
                &gatt_service_client_handle_gatt_client_event, 
                connection->con_handle, 
                &service);
            break;

        case GATT_SERVICE_CLIENT_STATE_W2_QUERY_CHARACTERISTIC_DESCRIPTORS:
#ifdef ENABLE_TESTING_SUPPORT
            printf("Read client characteristic descriptors for characteristic[%u, uuid16 0x%04x, value_handle 0x%04x]:\n", 
                connection->characteristic_index, 
                client->characteristics_desc16[connection->characteristic_index].uuid16,
                connection->characteristics[connection->characteristic_index].value_handle);
#endif
            connection->state = GATT_SERVICE_CLIENT_STATE_W4_CHARACTERISTIC_DESCRIPTORS_RESULT;
            characteristic.value_handle = connection->characteristics[connection->characteristic_index].value_handle;
            characteristic.properties   = connection->characteristics[connection->characteristic_index].properties;
            characteristic.end_handle   = connection->characteristics[connection->characteristic_index].end_handle;

            (void) gatt_client_discover_characteristic_descriptors(
                &gatt_service_client_handle_gatt_client_event, 
                connection->con_handle, 
                &characteristic);
            break;

        case GATT_SERVICE_CLIENT_STATE_W2_REGISTER_NOTIFICATION:
#ifdef ENABLE_TESTING_SUPPORT
            printf("Register notification for characteristic[%u, uuid16 0x%04x, ccd handle 0x%04x]:\n", 
                connection->characteristic_index, 
                client->characteristics_desc16[connection->characteristic_index].uuid16,
                connection->characteristics[connection->characteristic_index].client_configuration_handle);
#endif
            connection->state = GATT_SERVICE_CLIENT_STATE_W4_NOTIFICATION_REGISTERED;
            status = gatt_service_client_register_notification(connection);
            connection->characteristic_index++;
    
#ifdef ENABLE_TESTING_SUPPORT
            if (status != ERROR_CODE_SUCCESS) {
                printf("Notification not supported, status 0%02X\n.", status);
            }
#endif
            return;

        case GATT_SERVICE_CLIENT_STATE_CONNECTED:
            // TODO
            break;
        default:
            break;
    }

    if (status != ATT_ERROR_SUCCESS){
        gatt_service_client_emit_connected(client->packet_handler, connection->con_handle, connection->cid, status);
        gatt_service_client_finalize_connection(client, connection);
    }
}

// @return true if client valid / run function should be called
static bool gatt_service_client_handle_query_complete(gatt_service_client_connection_helper_t * connection, uint8_t status){
    btstack_assert(gatt_service_active_client != NULL);
    btstack_assert(connection != NULL);

    if (status != ATT_ERROR_SUCCESS){
        switch (connection->state){
            case GATT_SERVICE_CLIENT_STATE_W4_SERVICE_RESULT:
            case GATT_SERVICE_CLIENT_STATE_W2_QUERY_CHARACTERISTICS:
                gatt_service_client_emit_connected(gatt_service_active_client->packet_handler, connection->con_handle, connection->cid, status);
                gatt_service_client_finalize_connection(gatt_service_active_client, connection);
                return false;
            default:
                break;
        }
    }

    switch (connection->state){
        case GATT_SERVICE_CLIENT_STATE_W4_SERVICE_RESULT:
            if (connection->service_instances_num == 0){
                gatt_service_client_emit_connected(gatt_service_active_client->packet_handler, connection->con_handle, connection->cid, ERROR_CODE_UNSUPPORTED_FEATURE_OR_PARAMETER_VALUE);
                gatt_service_client_finalize_connection(gatt_service_active_client, connection);
                return false;
            }
            connection->state = GATT_SERVICE_CLIENT_STATE_W2_QUERY_CHARACTERISTICS;
            connection->characteristic_index = 0;
            break;
        
        case GATT_SERVICE_CLIENT_STATE_W4_CHARACTERISTIC_RESULT:
            connection->state = GATT_SERVICE_CLIENT_STATE_W2_QUERY_CHARACTERISTIC_DESCRIPTORS;
            connection->characteristic_index = 0;
            break;

        case GATT_SERVICE_CLIENT_STATE_W4_CHARACTERISTIC_DESCRIPTORS_RESULT:
            if (gatt_service_client_next_index_for_descriptor_query(connection)){
                connection->state = GATT_SERVICE_CLIENT_STATE_W2_QUERY_CHARACTERISTIC_DESCRIPTORS;
                break;
            }

            connection->characteristic_index = 0;
            if (gatt_service_client_next_index_for_notification_query(connection)){
                connection->state = GATT_SERVICE_CLIENT_STATE_W2_REGISTER_NOTIFICATION;
            } else {
                connection->characteristic_index = 0;
                connection->state = GATT_SERVICE_CLIENT_STATE_CONNECTED;
                gatt_service_client_emit_connected(gatt_service_active_client->packet_handler, connection->con_handle, connection->cid, ERROR_CODE_SUCCESS);
            }
            break;

        case GATT_SERVICE_CLIENT_STATE_W4_NOTIFICATION_REGISTERED:
            if (gatt_service_client_next_index_for_notification_query(connection)){
                connection->state = GATT_SERVICE_CLIENT_STATE_W2_REGISTER_NOTIFICATION;
                break;
            }

            connection->characteristic_index = 0;
            connection->state = GATT_SERVICE_CLIENT_STATE_CONNECTED;
            gatt_service_client_emit_connected(gatt_service_active_client->packet_handler, connection->con_handle, connection->cid, ERROR_CODE_SUCCESS);
            break;

        default:
            break;

    }
    // TODO run_for_client
    return true;
}

static uint8_t gatt_service_client_get_characteristic_index_for_uuid16(
        gatt_service_client_helper_t * client,
        uint16_t uuid16){
        uint8_t index = 0xff;

        uint8_t i;
        for (i = 0; i < client->characteristics_desc16_num; i++){
            if (client->characteristics_desc16[i].uuid16 == uuid16){
                index = i;
                break;
            }
        }
        return index;
}

static void gatt_service_client_handle_gatt_client_event(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size){
    UNUSED(packet_type); 
    UNUSED(channel);
    UNUSED(size);
    
    btstack_assert(gatt_service_active_client != NULL);

    gatt_service_client_connection_helper_t * connection = NULL;
    gatt_client_service_t service;
    gatt_client_characteristic_t characteristic;
    gatt_client_characteristic_descriptor_t characteristic_descriptor;
    bool call_run = true;
    uint8_t characteristic_index;

    switch (hci_event_packet_get_type(packet)){
        case GATT_EVENT_MTU:
            connection = gatt_service_client_get_connection_for_con_handle(gatt_service_active_client, gatt_event_mtu_get_handle(packet));
            btstack_assert(connection != NULL);
            connection->mtu = gatt_event_mtu_get_MTU(packet);
            break;

        case GATT_EVENT_SERVICE_QUERY_RESULT:
            connection = gatt_service_client_get_connection_for_con_handle(gatt_service_active_client, gatt_event_service_query_result_get_handle(packet));
            btstack_assert(connection != NULL);

            if (connection->service_instances_num < 1){
                gatt_event_service_query_result_get_service(packet, &service);
                connection->start_handle = service.start_group_handle;
                connection->end_handle   = service.end_group_handle;

#ifdef ENABLE_TESTING_SUPPORT
                printf("Service: start handle 0x%04X, end handle 0x%04X\n", connection->start_handle, connection->end_handle);
#endif          
                connection->service_instances_num++;
            } else {
                log_info("Found more then one Service instance.");
            }
            break;
 
        case GATT_EVENT_CHARACTERISTIC_QUERY_RESULT:
            connection = gatt_service_client_get_connection_for_con_handle(gatt_service_active_client, gatt_event_characteristic_query_result_get_handle(packet));
            btstack_assert(connection != NULL);
            gatt_event_characteristic_query_result_get_characteristic(packet, &characteristic);
      
            characteristic_index = gatt_service_client_get_characteristic_index_for_uuid16(gatt_service_active_client, characteristic.uuid16);
            if (characteristic_index < gatt_service_active_client->characteristics_desc16_num){
                connection->characteristics[characteristic_index].value_handle = characteristic.value_handle;
                connection->characteristics[characteristic_index].properties = characteristic.properties;
                connection->characteristics[characteristic_index].end_handle = characteristic.end_handle;
                connection->characteristics_num++;

#ifdef ENABLE_TESTING_SUPPORT
                printf("    [%u] Attribute Handle 0x%04X, Properties 0x%02X, Value Handle 0x%04X, UUID 0x%04X\n",
                       characteristic_index, characteristic.start_handle,
                       characteristic.properties, characteristic.value_handle, characteristic.uuid16);
#endif
            }
            break;

        case GATT_EVENT_ALL_CHARACTERISTIC_DESCRIPTORS_QUERY_RESULT:
            connection = gatt_service_client_get_connection_for_con_handle(gatt_service_active_client, gatt_event_all_characteristic_descriptors_query_result_get_handle(packet));
            btstack_assert(connection != NULL);
            gatt_event_all_characteristic_descriptors_query_result_get_characteristic_descriptor(packet, &characteristic_descriptor);
            
            if (characteristic_descriptor.uuid16 != ORG_BLUETOOTH_DESCRIPTOR_GATT_CLIENT_CHARACTERISTIC_CONFIGURATION){
                break;
            }
            btstack_assert(connection->state == GATT_SERVICE_CLIENT_STATE_W4_CHARACTERISTIC_DESCRIPTORS_RESULT);

            connection->characteristics[connection->characteristic_index].client_configuration_handle = characteristic_descriptor.handle;
            connection->characteristic_index++;

#ifdef ENABLE_TESTING_SUPPORT
            printf("    Characteristic Configuration Descriptor:  Handle 0x%04X, UUID 0x%04X\n", 
                characteristic_descriptor.handle,
                characteristic_descriptor.uuid16);
#endif
            break;

        case GATT_EVENT_QUERY_COMPLETE:
            connection = gatt_service_client_get_connection_for_con_handle(gatt_service_active_client, gatt_event_query_complete_get_handle(packet));
            btstack_assert(connection != NULL);
            call_run = gatt_service_client_handle_query_complete(connection, gatt_event_query_complete_get_att_status(packet));
            break;

        default:
            break;
    }

    if (call_run && (connection != NULL)){
        gatt_service_client_run_for_client(gatt_service_active_client, connection);
    }
}

void gatt_service_client_init(
        gatt_service_client_helper_t * client,
        void (*hci_event_handler_trampoline)(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size)){

    client->cid_counter = 0;
    client->characteristics_desc16_num = 0;
    client->hci_event_callback_registration.callback = hci_event_handler_trampoline;
    hci_add_event_handler(&client->hci_event_callback_registration);
}

// static btstack_packet_handler_t gatt_service_client_callback;

void gatt_service_client_register_packet_handler(gatt_service_client_helper_t * client, btstack_packet_handler_t packet_hander){
    btstack_assert(client != NULL);
    btstack_assert(packet_hander != NULL);

    client->packet_handler = packet_hander;
}

uint8_t gatt_service_client_connect(
        hci_con_handle_t con_handle,
        gatt_service_client_helper_t * client, gatt_service_client_connection_helper_t * connection, 
        gatt_service_client_characteristic_t * characteristics, uint8_t characteristics_num,
        btstack_packet_handler_t packet_handler, uint16_t * connection_cid){
    
    btstack_assert(client          != NULL);
    btstack_assert(connection      != NULL);
    btstack_assert(packet_handler  != NULL);
    btstack_assert(characteristics != NULL);
    
    if (gatt_service_client_get_connection_for_con_handle(client, con_handle) != NULL){
        return ERROR_CODE_COMMAND_DISALLOWED;
    }

    uint16_t cid = gatt_service_client_get_next_cid(client);
    if (connection_cid != NULL) {
        *connection_cid = cid;
    }
    
    connection->state = GATT_SERVICE_CLIENT_STATE_W2_QUERY_SERVICE;
    connection->cid                 = *connection_cid;
    connection->con_handle          = con_handle;
    connection->characteristics_num = 0;
    connection->characteristics     = characteristics;
    btstack_linked_list_add(&client->connections, (btstack_linked_item_t *) connection);

    gatt_service_active_client = client;

    gatt_service_client_run_for_client(client, connection);
    return ERROR_CODE_SUCCESS;
}

uint8_t gatt_service_client_disconnect(gatt_service_client_helper_t * client, uint16_t connection_cid){
    btstack_assert(client != NULL);

    gatt_service_client_connection_helper_t * connection = gatt_service_client_get_connection_for_cid(client, connection_cid);
    if (connection == NULL){
        return ERROR_CODE_UNKNOWN_CONNECTION_IDENTIFIER;
    }
    // finalize connections
    gatt_service_client_emit_disconnected(client->packet_handler, connection->con_handle, connection->cid);
    gatt_service_client_finalize_connection(client, connection);
    return ERROR_CODE_SUCCESS;
}

void gatt_service_client_deinit(gatt_service_client_helper_t * client){
    btstack_assert(client != NULL);

    client->packet_handler = NULL;

    client->cid_counter = 0;
    client->characteristics_desc16_num = 0;

    btstack_linked_list_iterator_t it;    
    btstack_linked_list_iterator_init(&it, (btstack_linked_list_t *) &client->connections);
    while (btstack_linked_list_iterator_has_next(&it)){
        gatt_service_client_connection_helper_t * connection = (gatt_service_client_connection_helper_t *)btstack_linked_list_iterator_next(&it);
        gatt_service_client_finalize_connection(client, connection);
    }
}


