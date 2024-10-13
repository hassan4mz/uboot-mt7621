char *buff = NULL;
char *md5_ptr = NULL;
char *size_ptr = NULL;
char size_str[16];
u8 md5_sum[16];
struct httpd_form_value *fw = NULL;
struct httpd_form_value *partition = NULL;
const struct fs_desc *file = NULL;
int i;

static const char hexchars[] = "0123456789abcdef";

// Initialize the buffer
memset(size_str, 0, sizeof(size_str));

// Find the form values
fw = httpd_request_find_value(request, "firmware");
partition = httpd_request_find_value(request, "partition");

if (!fw || !partition) {
    printf("Error: Missing firmware or partition data\n");
    response->info.code = 400;
    response->info.connection_close = 1;
    return;
}

// Validate partition value
if (strcmp(partition->data, "firmware") != 0 &&
    strcmp(partition->data, "bootloader") != 0 &&
    strcmp(partition->data, "factory") != 0) {
    printf("Error: Invalid partition selected\n");
    response->info.code = 400;
    response->info.connection_close = 1;
    return;
}

// Calculate MD5 sum
md5((u8 *)fw->data, fw->size, md5_sum);

// Allocate buffer for response
buff = malloc(response->size + 1);
if (!buff) {
    printf("Error: Failed to allocate memory for response buffer\n");
    response->info.code = 500;
    return;
}

// Copy response data and null-terminate
memcpy(buff, response->data, response->size);
buff[response->size] = '\0';

// Find MD5 and size placeholders
md5_ptr = strstr(buff, "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX");
size_ptr = strstr(buff, "YYYYYYYYYY");

if (md5_ptr) {
    for (i = 0; i < 16; i++) {
        u8 hex = (md5_sum[i] >> 4) & 0xf;
        md5_ptr[i * 2] = hexchars[hex];
        hex = md5_sum[i] & 0xf;
        md5_ptr[i * 2 + 1] = hexchars[hex];
    }
}

if (size_ptr) {
    snprintf(size_str, sizeof(size_str), "%-10zu", fw->size);
    memcpy(size_ptr, size_str, 10);
}

// Update response data
response->data = buff;

// Store upload information
upload_data_id = upload_id;
upload_data = fw->data;
upload_size = fw->size;
selected_partition = strdup(partition->data);

if (!selected_partition) {
    printf("Error: Failed to allocate memory for selected partition\n");
    free(buff);
    response->info.code = 500;
    return;
}

printf("Firmware upload received for partition: %s\n", selected_partition);
