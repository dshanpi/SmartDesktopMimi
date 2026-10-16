#include "iot_client.h"
#include "iot_json.h"
#include <curl/curl.h>
#include <errno.h>
#include <json-c/json.h>
#include <ctype.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#ifndef MIMI_IOT_CONFIG
#define MIMI_IOT_CONFIG "/etc/smartdesktopmimi/iot-client.json"
#endif
static pthread_once_t curl_once = PTHREAD_ONCE_INIT;
static void init_curl(void)
{
    curl_global_init(CURL_GLOBAL_DEFAULT);
}
struct result
{
    char * data;
    size_t used, capacity;
};
static size_t receive(void * ptr, size_t size, size_t count, void * user)
{
    struct result * r = user;
    if(size && count > SIZE_MAX / size) return 0;
    size_t n = size * count;
    if(n >= r->capacity - r->used || memchr(ptr, 0, n)) return 0;
    memcpy(r->data + r->used, ptr, n);
    r->used += n;
    r->data[r->used] = 0;
    return n;
}
static bool origin_valid(const char * s)
{
    if(strncmp(s, "https://", 8) || !s[8] || strlen(s) > 200) return false;
    for(const char * p = s + 8; *p; p++)
        if(!isalnum((unsigned char)*p) && *p != '.' && *p != '-' && *p != ':') return false;
    return true;
}
static bool token_valid(const char * s)
{
    size_t n = strlen(s);
    if(n < 32 || n > 200) return false;
    for(; *s; s++)
        if(!isalnum((unsigned char)*s) && *s != '-' && *s != '_') return false;
    return true;
}
int mimi_iot_request(const char * path, const char * command, char * out, size_t cap)
{
    if(!out || cap < 128) return -1;
    out[0] = 0;
    if(!path || strncmp(path, "/v1/devices", 11) || strlen(path) > 300) return -1;
    for(const char * p = path; *p; p++)
        if(!isalnum((unsigned char)*p) && *p != '/' && *p != '-' && *p != '_') return -1;
    int fd = open(MIMI_IOT_CONFIG, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    struct stat st;
    char raw[4097] = {0}, url[512], auth[224];
    if(fd < 0) {
        snprintf(out, cap, "{\"error\":\"not_configured\"}");
        return -2;
    }
    if(fstat(fd, &st) || !S_ISREG(st.st_mode) || (st.st_mode & 0077) || st.st_uid != geteuid() || st.st_size < 2 ||
       st.st_size > 4096) {
        close(fd);
        snprintf(out, cap, "{\"error\":\"invalid_private_configuration\"}");
        return -2;
    }
    ssize_t n = read(fd, raw, sizeof(raw) - 1);
    close(fd);
    if(n != st.st_size) return -2;
    json_object *config = mimi_iot_json_object(raw, (size_t)n), *endpoint = NULL, *token = NULL;
    bool valid = config && json_object_get_type(config) == json_type_object && json_object_object_length(config) == 2 &&
                 json_object_object_get_ex(config, "endpoint", &endpoint) &&
                 json_object_get_type(endpoint) == json_type_string &&
                 json_object_object_get_ex(config, "token", &token) &&
                 json_object_get_type(token) == json_type_string &&
                 strlen(json_object_get_string(endpoint)) == (size_t)json_object_get_string_len(endpoint) &&
                 strlen(json_object_get_string(token)) == (size_t)json_object_get_string_len(token) && origin_valid(json_object_get_string(endpoint)) &&
                 token_valid(json_object_get_string(token));
    if(!valid) {
        if(config) json_object_put(config);
        memset(raw, 0, sizeof(raw));
        snprintf(out, cap, "{\"error\":\"invalid_private_configuration\"}");
        return -2;
    }
    snprintf(url, sizeof(url), "%s%s", json_object_get_string(endpoint), path);
    snprintf(auth, sizeof(auth), "Authorization: Bearer %s", json_object_get_string(token));
    json_object_put(config);
    memset(raw, 0, sizeof(raw));
    pthread_once(&curl_once, init_curl);
    CURL * curl = curl_easy_init();
    if(!curl) {
        memset(auth, 0, sizeof(auth));
        return -1;
    }
    struct curl_slist * headers = NULL;
    headers                     = curl_slist_append(headers, auth);
    headers                     = curl_slist_append(headers, "Content-Type: application/json");
    memset(auth, 0, sizeof(auth));
    struct result r = {out, 0, cap};
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 20L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS, CURLPROTO_HTTPS);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, receive);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &r);
    if(command) {
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, command);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)strlen(command));
    }
    CURLcode status = curl_easy_perform(curl);
    long code       = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    if(status != CURLE_OK) {
        snprintf(out, cap, "{\"error\":\"connection_failed\"}");
        return -1;
    }
    if(code != 200) {
        if(code == 401 || code == 403) snprintf(out, cap, "{\"error\":\"authorization_required\"}");
        return -3;
    }
    return 0;
}

int mimi_iot_configuration_present(void)
{
 struct stat st;
 if(lstat(MIMI_IOT_CONFIG,&st)==0)return S_ISREG(st.st_mode)?1:-1;
 return errno==ENOENT?0:-1;
}
