static void result_handler(enum httpd_uri_handler_status status,
    struct httpd_request *request,
    struct httpd_response *response)
{
    const struct fs_desc *file;
    struct flashing_status *st;
    char *result_message;

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

        u32 size = http_make_response_header(&response->info,
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

        if (upload_data_id == rand()) {  // Proper ID comparison
            switch (upload_type) {
                case 0:
                    st->ret = write_firmware_failsafe((size_t) upload_data, upload_size);
                    result_message = st->ret ? "Firmware upgrade failed!" : "Firmware upgrade successful!";
                    break;
                case 1:
                    st->ret = write_bootloader_failsafe(upload_data, upload_size);
                    result_message = st->ret ? "Bootloader upgrade failed!" : "Bootloader upgrade successful!";
                    break;
                case 2:
                    st->ret = write_factory_partition(upload_data, upload_size);
                    result_message = st->ret ? "Factory partition upgrade failed!" : "Factory partition upgrade successful!";
                    break;
                default:
                    st->ret = -1;
                    result_message = "Unknown upgrade type!";
            }
        } else {
            result_message = "Invalid upload session!";
        }

        // Invalidate upload identifier
        upload_data_id = rand();

        if (!st->ret)
            file = fs_find_file("success.html");
        else
            file = fs_find_file("fail.html");

        if (!file) {
            response->data = result_message;
            response->size = strlen(response->data);
            return;
        }

        // Replace placeholder in HTML file with result message
        char *html_content = malloc(file->size + strlen(result_message));
        if (html_content) {
            char *placeholder = strstr(file->data, "{{RESULT_MESSAGE}}");
            if (placeholder) {
                size_t prefix_len = placeholder - (char *)file->data;
                memcpy(html_content, file->data, prefix_len);
                strcpy(html_content + prefix_len, result_message);
                strcpy(html_content + prefix_len + strlen(result_message), 
                       placeholder + strlen("{{RESULT_MESSAGE}}"));
                
                response->data = html_content;
                response->size = strlen(html_content);
            } else {
                free(html_content);
                response->data = file->data;
                response->size = file->size;
            }
        } else {
            response->data = file->data;
            response->size = file->size;
        }

        st->body_sent = 1;

        return;
    }

    if (status == HTTP_CB_CLOSED) {
        st = response->session_data;
        upgrade_success = !st->ret;
        if (response->data != st->buf && response->data != fs_find_file("success.html")->data && response->data != fs_find_file("fail.html")->data) {
            free((void *)response->data);
        }
        free(response->session_data);
        if (upgrade_success)
            tcp_close_all_conn();
    }
}
