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

// ... (keep the existing code)

static void upload_handler(enum httpd_uri_handler_status status,
    struct httpd_request *request,
    struct httpd_response *response)
{
    char *buff, *md5_ptr, *size_ptr, size_str[16];
    u8 md5_sum[16];
    struct httpd_form_value *fw, *partition;
    const struct fs_desc *file;
    int i;

    static char hexchars[] = "0123456789abcdef";

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

        // ... (keep the existing code)

        return;
    }

    // ... (keep the existing code)
}

static void result_handler(enum httpd_uri_handler_status status,
    struct httpd_request *request,
    struct httpd_response *response)
{
    const struct fs_desc *file;
    struct flashing_status *st;
    u32 size;

    if (status == HTTP_CB_NEW) {
        // ... (keep the existing code)
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

        // ... (keep the existing code)
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

// ... (keep the remaining code)
