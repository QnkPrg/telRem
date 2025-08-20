#include <string.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "audio_common.h"
#include "freertos/FreeRTOS.h"
#include "audio_mem.h"
#include "audio_element.h"
#include "esp_log.h"
#include "udp_stream.h"

#define AUDIO_PACKAGE 0
#define FEC_PACKAGE 1

// UDP Stream packet header structure:
// 1 Byte for package type
// 4 Bytes for package sequence number
// 8 Bytes for timestamp
// 2 Bytes for package length
// Total header length is 15 Bytes (1 + 4 + 8 + 2)
#define UDP_STREAM_HEADER_LEN 15 // Header length for UDP stream packets

// Header field offsets
#define UDP_HEADER_TYPE_OFFSET      0   // Package type (1 byte)
#define UDP_HEADER_SEQUENCE_OFFSET  1   // Sequence number (4 bytes)
#define UDP_HEADER_TIMESTAMP_OFFSET 5   // Timestamp (8 bytes)
#define UDP_HEADER_LENGTH_OFFSET    13  // Packet length (2 bytes)
#define UDP_HEADER_DATA_OFFSET      15  // Start of data payload

// Header field sizes
#define UDP_HEADER_TYPE_SIZE        1
#define UDP_HEADER_SEQUENCE_SIZE    4
#define UDP_HEADER_TIMESTAMP_SIZE   8
#define UDP_HEADER_LENGTH_SIZE      2

static const char *TAG = "udp_STREAM";

typedef struct udp_stream {
    audio_stream_type_t type; // Type of the audio stream
    int sock; // Socket for UDP communication
    struct sockaddr_in dest_addr; // Destination address for UDP stream
    bool is_open; // Flag to indicate if the stream is open
    int buffer_len; // Buffer length for the stream
} udp_stream_t;

static esp_err_t _udp_open(audio_element_handle_t self)
{
    udp_stream_t *udp = (udp_stream_t *)audio_element_getdata(self);
    if(udp->is_open)
        return ESP_OK;

    if (udp->type == AUDIO_STREAM_WRITER) {
        ESP_LOGI(TAG, "AUDIO_STREAM_WRITER");
    }
    else{
        ESP_LOGI(TAG, "AUDIO_STREAM_READER");
    }

        // Create shared UDP socket
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        ESP_LOGE(TAG, "Failed to create socket");
        return ESP_FAIL;
    }

    struct sockaddr_in local_addr = {
        .sin_family = udp->dest_addr.sin_family,
        .sin_addr.s_addr = htonl(INADDR_ANY),
        .sin_port = udp->dest_addr.sin_port,
    };

    if(udp->type == AUDIO_STREAM_READER){
        if (bind(sock, (struct sockaddr *)&local_addr, sizeof(local_addr)) < 0) {
            ESP_LOGE(TAG, "Socket bind failed: errno %d", errno);
            close(sock);
            return ESP_FAIL;
        }
    }

    udp->sock = sock;
    udp->is_open = true;

    return ESP_OK;
}

static esp_err_t _udp_close(audio_element_handle_t self)
{
    udp_stream_t *udp = (udp_stream_t *)audio_element_getdata(self);
    if(!udp->is_open){
        ESP_LOGW(TAG, "UDP stream already closed");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Closing UDP stream");
    
    // Shutdown socket to interrupt any pending recv/send operations
    if (udp->sock >= 0) {
        close(udp->sock);
        udp->sock = -1;
    }
    
    udp->is_open = false;
    
    if (AEL_STATE_PAUSED != audio_element_get_state(self)) {
        audio_element_report_pos(self);
        audio_element_set_byte_pos(self, 0);
    }

    ESP_LOGI(TAG, "Closed UDP stream");
    
    return ESP_OK;
}

static int _udp_stream_read(audio_element_handle_t self, char *buffer, int len, TickType_t ticks_to_wait, void *context)
{
    udp_stream_t *udp = (udp_stream_t *)audio_element_getdata(self);
    static int pckg_count = 0;
    struct timeval timeout;
    
    if (!udp->is_open) {
        ESP_LOGW(TAG, "UDP stream not open");
        return AEL_IO_FAIL;
    }

    if(len < 0) {
        return len;
    }

    if (ticks_to_wait == portMAX_DELAY) {
        timeout.tv_sec = 0;
        timeout.tv_usec = 100000; // 100ms default timeout to prevent indefinite blocking
    } 
    else {
        uint32_t timeout_ms = ticks_to_wait * portTICK_PERIOD_MS;
        timeout.tv_sec = timeout_ms / 1000;
        timeout.tv_usec = (timeout_ms % 1000) * 1000;
    }
    setsockopt(udp->sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    
    int ret = recvfrom(udp->sock, buffer, len, 0, NULL, NULL);
    
    if (ret < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            ESP_LOGD(TAG, "UDP recv timeout");
            return AEL_IO_TIMEOUT; // Let pipeline retry
        }
        ESP_LOGE(TAG, "UDP recv error: errno %d", errno);
        audio_element_report_status(self, AEL_STATUS_ERROR_INPUT);
        return AEL_IO_FAIL;
    }
    pckg_count++;
    ESP_LOGD(TAG, "UDP packet count: %d", pckg_count);
    
    if (ret > 0) {
        audio_element_update_byte_pos(self, ret);
    }
    
    return ret;
}

static int _udp_stream_write(audio_element_handle_t self, char *buffer, int len, TickType_t ticks_to_wait, void *context)
{
    udp_stream_t *udp = (udp_stream_t *)audio_element_getdata(self);
    int ret;
    static uint32_t sequence_number = 0; // Sequence number for packets

    if(!udp->is_open) {
        ESP_LOGW(TAG, "UDP stream not open");
        return AEL_IO_FAIL;
    }

    if (len < 0) {
        return len;
    }
    
    else if(len == 0) {
        ESP_LOGD(TAG, "Write received zero-length buffer, ignoring");
        return AEL_IO_OK;
    }
    if (len > udp->buffer_len) {
        ESP_LOGW(TAG, "Write buffer length %d exceeds configured limit %d, truncating", len, udp->buffer_len);
        len = udp->buffer_len; // Truncate to configured buffer length
    }

    // Build audio packet header in separate buffer
    uint8_t header[UDP_STREAM_HEADER_LEN];
    struct timeval tv;
    gettimeofday(&tv, NULL);
    int64_t time_ms = (int64_t)tv.tv_sec * 1000L + (int64_t)tv.tv_usec / 1000L;
    uint16_t packet_length = len;
    
    // Audio packet header construction
    header[UDP_HEADER_TYPE_OFFSET] = AUDIO_PACKAGE;
    memcpy(&header[UDP_HEADER_SEQUENCE_OFFSET], &sequence_number, UDP_HEADER_SEQUENCE_SIZE);
    memcpy(&header[UDP_HEADER_TIMESTAMP_OFFSET], &time_ms, UDP_HEADER_TIMESTAMP_SIZE);
    memcpy(&header[UDP_HEADER_LENGTH_OFFSET], &packet_length, UDP_HEADER_LENGTH_SIZE);

    struct iovec iov[2];
    iov[0].iov_base = header;
    iov[0].iov_len = UDP_STREAM_HEADER_LEN;
    iov[1].iov_base = buffer;
    iov[1].iov_len = len;

    struct msghdr msg = {
        .msg_name = &udp->dest_addr,
        .msg_namelen = sizeof(udp->dest_addr),
        .msg_iov = iov,
        .msg_iovlen = 2,
        .msg_control = NULL,
        .msg_controllen = 0,
        .msg_flags = 0
    };

    if((ret = sendmsg(udp->sock, &msg, 0)) < 0){
        ESP_LOGE(TAG, "UDP send failed: errno %d (%s) ; len %d", errno,strerror(errno), len);
        if(errno == ENOMEM){
            ESP_LOGW(TAG,"NO MEM %d", len);
            return len; // Pretend success to avoid pipeline errors, discard data
        }
        audio_element_report_status(self, AEL_STATUS_ERROR_OUTPUT);
        return AEL_IO_FAIL;
    }

    sequence_number++; // Increment for next packet
    return ret;
}

static int _udp_stream_process(audio_element_handle_t self, char *in_buffer, int in_len)
{

    if(in_len < 0)
        return in_len;
    int r_size = audio_element_input(self, in_buffer, in_len);
    int w_size = 0;
    
    if (r_size == AEL_IO_TIMEOUT) {
        // Continue with silence during timeout
        memset(in_buffer, 0x00, in_len);
        w_size = audio_element_output(self, in_buffer, in_len);
    }
    else if(r_size > 0){
        w_size = audio_element_output(self, in_buffer, r_size);
        if (w_size > 0) {
            audio_element_update_byte_pos(self, w_size);
        }
    }
    else{
        // Propagate error/done status
        w_size = r_size;
    }

    return w_size;
}

static esp_err_t _udp_destroy(audio_element_handle_t self)
{
    udp_stream_t *udp = (udp_stream_t *)audio_element_getdata(self);
    if (udp) {        
        ESP_LOGI(TAG, "Destroying UDP stream");
        audio_free(udp);
    }
    return ESP_OK;
}

audio_element_handle_t udp_stream_init(udp_stream_cfg_t *config)
{

    udp_stream_t *udp = audio_calloc(1, sizeof(udp_stream_t));
    AUDIO_MEM_CHECK(TAG, udp, return NULL);

    udp->type = config->type;
    udp->sock = -1;
    udp->dest_addr = config->dest_addr;
    udp->is_open = false;
    udp->buffer_len = config->buffer_len;

    audio_element_cfg_t cfg = DEFAULT_AUDIO_ELEMENT_CONFIG();
    cfg.buffer_len = config->buffer_len;
    if (config -> task_stack < 4096) {
        cfg.task_stack = 4096; // Default minimum stack size
    } else {
        cfg.task_stack = config->task_stack;
    }
    cfg.open = _udp_open;
    cfg.close = _udp_close;
    cfg.destroy = _udp_destroy;
    cfg.tag = "udp";
    cfg.out_rb_size = config->out_rb_size;
    cfg.process = _udp_stream_process;
    if(udp->type == AUDIO_STREAM_WRITER)
        cfg.write = _udp_stream_write;
    else
        cfg.read = _udp_stream_read;
    

    audio_element_handle_t el = audio_element_init(&cfg);
    AUDIO_MEM_CHECK(TAG, el, {
        audio_free(udp);
        return NULL;
    });

    audio_element_setdata(el, udp);
    ESP_LOGI(TAG, "UDP stream initialized: %s",
             udp->type == AUDIO_STREAM_WRITER ? "writer" : "reader");
    return el;
}
