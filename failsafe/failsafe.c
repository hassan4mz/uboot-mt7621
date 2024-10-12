/* SPDX-License-Identifier:	GPL-2.0 */
/*
 * Copyright (C) 2019 MediaTek Inc. All Rights Reserved.
 *
 * Author: Weijie Gao <weijie.gao@mediatek.com>
 *
 */

#include <common.h>
#include <malloc.h>
#include <net/tcp.h>
#include <net/httpd.h>
#include <u-boot/md5.h>

#include "fs.h"

static u32 upload_data_id;
static const void *upload_data;
static size_t upload_size;
static int upgrade_success;
static char *selected_partition;

extern int write_firmware_failsafe(size_t data_addr, uint32_t data_size);
extern int write_bootloader_failsafe(size_t data_addr, uint32_t data_size);
extern int write_factory_failsafe(size_t data_addr, uint32_t data_size);

struct flashing_status {
    char buf[4096];
    int ret;
    int body_sent;
};

static void upload_handler(enum httpd_uri_handler_status status,
    struct httpd_request *request,
    struct httpd_response *response)
{
    struct httpd_form_value *fw, *partition;

    if (status == HTTP_CB_NEW) {
        fw = httpd_request_find_value(request, "firmware");
        partition = httpd_request_find_value(request, "partition");
        if (!fw || !partition) {
            response->info.code = 302;
            response->info.connection_close = 1;
            response->info.location = "/";
            return;
        }

        // Store the selected partition
        selected_partition = strdup(partition->data);

        upload_data_id = upload_id;
        upload_data = fw->data;
        upload_size = fw->size;

        // Redirect to flashing page
        response->info.code = 302;
        response->info.connection_close = 1;
        response->info.location = "/flashing";
        return;
    }
}

static void result_handler(enum httpd_uri_handler_status status,
    struct httpd_request *request,
    struct httpd_response *response)
{
    struct flashing_status *st;

    if (status == HTTP_CB_NEW) {
        st = calloc(1, sizeof(*st));
        if (!st) {
            response->info.code = 500;
            return;
        }

        st->ret = -1;

        response->session_data = st;

        response->status = HTTP_RESP_CUSTOM;

        response->info.http_1_0 = 1;
        response->info.content_length = -1;
        response->info.connection_close = 1;
        response->info.content_type = "text/html";
        response->info.code = 200;

        response->data = st->buf;
        response->size = http_make_response_header(&response->info,
            st->buf, sizeof(st->buf));

        return;
    }

    if (status == HTTP_CB_RESPONDING) {
        st = response->session_data;

        if (st->body_sent) {
            response->status = HTTP_RESP_NONE;
            return;
        }

        if (upload_data_id == upload_id) {
            if (strcmp(selected_partition, "firmware") == 0) {
                st->ret = write_firmware_failsafe((size_t) upload_data, upload_size);
            } else if (strcmp(selected_partition, "bootloader") == 0) {
                st->ret = write_bootloader_failsafe((size_t) upload_data, upload_size);
            } else if (strcmp(selected_partition, "factory") == 0) {
                st->ret = write_factory_failsafe((size_t) upload_data, upload_size);
            } else {
                st->ret = -1; // Invalid partition
            }
        }

        // invalidate upload identifier
        upload_data_id = rand();

        if (!st->ret)
            response->data = "Upgrade completed!";
        else
            response->data = "Upgrade failed!";
        response->size = strlen(response->data);

        st->body_sent = 1;

        return;
    }

    if (status == HTTP_CB_CLOSED) {
        st = response->session_data;

        upgrade_success = !st->ret;

        free(response->session_data);
        free(selected_partition);
        
        if (upgrade_success)
            tcp_close_all_conn();
    }
}

// ... (keep the remaining code, including start_web_failsafe and do_httpd functions)

// Add these lines at the end of the file to use the functions and avoid unused function warnings
void __attribute__((used)) dummy_function(void) {
    upload_handler(HTTP_CB_NEW, NULL, NULL);
    result_handler(HTTP_CB_NEW, NULL, NULL);
}
