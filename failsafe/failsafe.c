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

static int output_plain_file(struct httpd_response *response,
    const char *filename)
{
    const struct fs_desc *file;
    int ret = 0;

    file = fs_find_file(filename);

    response->status = HTTP_RESP_STD;

    if (file) {
        response->data = file->data;
        response->size = file->size;
    } else {
        response->data = "Error: file not found";
        response->size = strlen(response->data);
        ret = 1;
    }

    response->info.code = 200;
    response->info.connection_close = 1;
    response->info.content_type = "text/html";

    return ret;
}

static void index_handler(enum httpd_uri_handler_status status,
    struct httpd_request *request,
    struct httpd_response *response)
{
    if (status == HTTP_CB_NEW)
        output_plain_file(response, "index.html");
}

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

        if (output_plain_file(response, "upload.html")) {
            response->info.code = 500;
            return;
        }

        upload_data_id = upload_id;
        upload_data = fw->data;
        upload_size = fw->size;

        return;
    }

    if (status == HTTP_CB_CLOSED) {
        const struct fs_desc *file = fs_find_file("upload.html");

        if (file) {
            if (file->data != response->data)
                free((void *) response->data);
        }
    }
}

static void flashing_handler(enum httpd_uri_handler_status status,
    struct httpd_request *request,
    struct httpd_response *response)
{
    if (status == HTTP_CB_NEW)
        output_plain_file(response, "flashing.html");
}

static void result_handler(enum httpd_uri_handler_status status,
    struct httpd_request *request,
    struct httpd_response *response)
{
    const struct fs_desc *file;
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

        uint32_t size = http_make_response_header(&response->info,
            st->buf, sizeof(st->buf));

        response->data = st->buf;
        response->size = size;

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
            } else if (strcmp(selected_partition, "factory") == 0) {
                st->ret = write_factory_failsafe((size_t) upload_data, upload_size);
            } else {
                st->ret = -1; // Invalid partition
            }
        }

        // invalidate upload identifier
        upload_data_id = rand();

        if (!st->ret)
            file = fs_find_file("success.html");
        else
            file = fs_find_file("fail.html");

        if (!file) {
            if (!st->ret)
                response->data = "Upgrade completed!";
            else
                response->data = "Upgrade failed!";
            response->size = strlen(response->data);
            return;
        }

        response->data = file->data;
        response->size = file->size;

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

static void style_handler(enum httpd_uri_handler_status status,
    struct httpd_request *request,
    struct httpd_response *response)
{
    if (status == HTTP_CB_NEW) {
        output_plain_file(response, "style.css");
        response->info.content_type = "text/css";
    }
}

static void not_found_handler(enum httpd_uri_handler_status status,
    struct httpd_request *request,
    struct httpd_response *response)
{
    if (status == HTTP_CB_NEW) {
        output_plain_file(response, "404.html");
        response->info.code = 404;
    }
}

int start_web_failsafe(void)
{
    struct httpd_instance *inst;

    inst = httpd_find_instance(80);
    if (inst)
        httpd_free_instance(inst);

    inst = httpd_create_instance(80);
    if (!inst) {
        printf("Error: failed to create HTTP instance on port 80\n");
        return -1;
    }

    httpd_register_uri_handler(inst, "/", &index_handler, NULL);
    httpd_register_uri_handler(inst, "/cgi-bin/luci", &index_handler, NULL);
    httpd_register_uri_handler(inst, "/upload", &upload_handler, NULL);
    httpd_register_uri_handler(inst, "/flashing", &flashing_handler, NULL);
    httpd_register_uri_handler(inst, "/result", &result_handler, NULL);
    httpd_register_uri_handler(inst, "/style.css", &style_handler, NULL);
    httpd_register_uri_handler(inst, "", &not_found_handler, NULL);

    net_loop(TCP);

    return 0;
}

static int do_httpd(cmd_tbl_t *cmdtp, int flag, int argc,
    char *const argv[])
{
    int ret;

    printf("\nWeb failsafe UI started\n");
    
    ret = start_web_failsafe();

    if (upgrade_success)
        do_reset(NULL, 0, 0, NULL);

    return ret;
}

U_BOOT_CMD(httpd, 1, 0, do_httpd,
    "Start failsafe HTTP server", ""
);
