/**
 * @file tuya_os_adapt_storge.c
 * @brief flash操作接口
 * 
 * @copyright Copyright(C),2018-2020, 涂鸦科技 www.tuya.com
 * 
 */
#include <dirent.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <stdio.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include "tkl_flash.h"
#include "tkl_fs.h"

#ifndef FLASH_FILE_PATH
#define FLASH_FILE_PATH "./tuyadb"
#endif
#define FLASH_FILE_NAME ""FLASH_FILE_PATH"/tuyadb"
#ifndef TUYA_LICENSE_FILE_PATH
#define TUYA_LICENSE_FILE_PATH "/factory/tuya/license.env"
#endif
#ifndef FLASH_FILE_SIZE
#define FLASH_FILE_SIZE (1 * 1024 * 1024)
#endif
#define FLASH_BASE_ADDR 0X00 //

#define PARTITION_SIZE (1 << 12) /* 4KB */

//key
#define SIMPLE_FLASH_KEY_ADDR FLASH_BASE_ADDR

//KV
#define SIMPLE_FLASH_START (SIMPLE_FLASH_KEY_ADDR + PARTITION_SIZE)
#define SIMPLE_FLASH_SIZE 0x8000 // 32K

//UF
#define UF_PARTITION_NUM     1
#define UF_PARTITION_START  (SIMPLE_FLASH_START + SIMPLE_FLASH_SIZE)
#define UF_PARTITION_SIZE   0x18000 // 96K

#define RCD_FILE_START      (UF_PARTITION_START + UF_PARTITION_SIZE)
#define RCD_FILE_SIZE       0x19000 // 100k

#if defined(KV_PROTECTED_ENABLE) && (KV_PROTECTED_ENABLE==1)
#define SIMPLE_FLASH_KV_PROTECTED_START (RCD_FILE_START + RCD_FILE_SIZE)
#define SIMPLE_FLASH_KV_PROTECTED_SIZE 0x1000
#endif

typedef struct {
    char *uuid;
    char *authkey;
} tuya_iot_license_t;

static TUYA_FILE s_flash_file = 0;
static char s_license_uuid[21] = {0};
static char s_license_authkey[33] = {0};

static int __license_value_valid(const char *value, size_t expected_length)
{
    size_t i;

    if (strlen(value) != expected_length) {
        return 0;
    }
    for (i = 0; i < expected_length; i++) {
        unsigned char ch = (unsigned char)value[i];
        if (ch <= 0x20 || ch > 0x7e) {
            return 0;
        }
    }
    return 1;
}

static char *__license_trim(char *value)
{
    char *end;

    while (*value == ' ' || *value == '\t') {
        value++;
    }

    end = value + strlen(value);
    while (end > value && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r')) {
        *--end = '\0';
    }

    return value;
}

static OPERATE_RET __license_parse(char *content, tuya_iot_license_t *license)
{
    char *saveptr = NULL;
    char *line;
    int uuid_seen = 0;
    int authkey_seen = 0;

    for (line = strtok_r(content, "\n", &saveptr); line != NULL;
         line = strtok_r(NULL, "\n", &saveptr)) {
        char *key = __license_trim(line);
        char *separator;
        char *value;

        if (*key == '\0' || *key == '#') {
            continue;
        }

        separator = strchr(key, '=');
        if (separator == NULL) {
            return OPRT_COM_ERROR;
        }
        *separator = '\0';
        key = __license_trim(key);
        value = __license_trim(separator + 1);

        if (strcmp(key, "TUYA_OPENSDK_UUID") == 0) {
            if (uuid_seen || !__license_value_valid(value, 20)) {
                return OPRT_COM_ERROR;
            }
            memcpy(s_license_uuid, value, 20);
            s_license_uuid[20] = '\0';
            uuid_seen = 1;
        } else if (strcmp(key, "TUYA_OPENSDK_AUTHKEY") == 0) {
            if (authkey_seen || !__license_value_valid(value, 32)) {
                return OPRT_COM_ERROR;
            }
            memcpy(s_license_authkey, value, 32);
            s_license_authkey[32] = '\0';
            authkey_seen = 1;
        } else {
            return OPRT_COM_ERROR;
        }
    }

    if (!uuid_seen || !authkey_seen) {
        memset(s_license_uuid, 0, sizeof(s_license_uuid));
        memset(s_license_authkey, 0, sizeof(s_license_authkey));
        return OPRT_COM_ERROR;
    }

    license->uuid = s_license_uuid;
    license->authkey = s_license_authkey;
    return OPRT_OK;
}

/**
 * @brief read data from flash
 * 
 * @param[in]       addr        flash address
 * @param[out]      dst         pointer of buffer
 * @param[in]       size        size of buffer
 * @return int 0=成功，非0=失败
 */
OPERATE_RET tkl_flash_read(uint32_t addr, uint8_t *dst, uint32_t size)
{
    int ret = 0;
    if(!s_flash_file) {
        return OPRT_RESOURCE_NOT_READY;
    }

    if(0 != tkl_fseek(s_flash_file, addr, SEEK_SET)) {
        return OPRT_FILE_OPEN_FAILED;
    }

    ret = tkl_fread((void*)dst, size, s_flash_file);
    if(size != ret) {
        return OPRT_FILE_READ_FAILED;
    }

    return OPRT_OK;
}

/**
 * @brief write data to flash
 * 
 * @param[in]       addr        flash address
 * @param[in]       src         pointer of buffer
 * @param[in]       size        size of buffer
 * @return int 0=成功，非0=失败
 */
OPERATE_RET tkl_flash_write(uint32_t addr, const uint8_t *src, uint32_t size)
{
    if(!s_flash_file) {
        return OPRT_RESOURCE_NOT_READY;
    }

    if(0 != tkl_fseek(s_flash_file, addr, SEEK_SET)) {
        return OPRT_FILE_OPEN_FAILED;
    }

    if(size != tkl_fwrite((void*)src, size, s_flash_file)) {
        return OPRT_FILE_WRITE_FAILED;
    }

    tkl_fflush(s_flash_file);
    tkl_fsync(tkl_fileno(s_flash_file));

    return OPRT_OK;
}

/**
 * @brief erase flash block
 * 
 * @param[in]       addr        flash block address
 * @param[in]       size        size of flash block
 * @return int 0=成功，非0=失败
 */
OPERATE_RET tkl_flash_erase(uint32_t addr, uint32_t size)
{
    return OPRT_OK;
}

static OPERATE_RET __flash_file_init(void)
{
    char *data = malloc(PARTITION_SIZE);
    if(NULL == data) {
        return OPRT_MALLOC_FAILED;
    }

    memset(data, 0xff, PARTITION_SIZE);
    int offset = 0;

    for(offset = 0; offset < FLASH_FILE_SIZE; offset += PARTITION_SIZE) {
        tkl_fwrite(data, PARTITION_SIZE, s_flash_file);
    }
    free(data);
    tkl_fflush(s_flash_file);
    tkl_fsync(tkl_fileno(s_flash_file));

    // extern OPERATE_RET ws_db_init_mf(void);
    // ws_db_init_mf();

    return OPRT_OK;
}

/**
* @brief get one flash type info
*
* @param[in] type: flash type
* @param[in] info: flash info
*
* @note This API is used for unlock flash.
*
* @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
*/
OPERATE_RET tkl_flash_get_one_type_info(TUYA_FLASH_TYPE_E type, TUYA_FLASH_BASE_INFO_T* info)
{
    BOOL_T is_exist = FALSE;

    if(!s_flash_file) {
        tkl_fs_is_exist(FLASH_FILE_NAME, &is_exist);
        if(FALSE == is_exist) {
            tkl_fs_mkdir(FLASH_FILE_PATH);
            s_flash_file = tkl_fopen(FLASH_FILE_NAME, "wb+");
            __flash_file_init();
        } else {
            s_flash_file = tkl_fopen(FLASH_FILE_NAME, "rb+");
        }
        int fd = tkl_fileno(s_flash_file);
        if (fd >=0 ) {
            fcntl(fd, F_SETFD, FD_CLOEXEC);
        } 
    }

    if ((type > TUYA_FLASH_TYPE_MAX) || (info == NULL)) {
        return OPRT_INVALID_PARM;
    }
    switch (type) {
        case TUYA_FLASH_TYPE_UF:
            info->partition_num = 1;
            info->partition[0].block_size =  PARTITION_SIZE;
            info->partition[0].start_addr = UF_PARTITION_START;
            info->partition[0].size = UF_PARTITION_SIZE;
            break;
       case TUYA_FLASH_TYPE_KV_DATA:
            info->partition_num = 1;
            info->partition[0].block_size = PARTITION_SIZE;
            info->partition[0].start_addr = SIMPLE_FLASH_START;
            info->partition[0].size = SIMPLE_FLASH_SIZE;
            break;
       case TUYA_FLASH_TYPE_KV_KEY:
            info->partition_num = 1;
            info->partition[0].block_size = PARTITION_SIZE;
            info->partition[0].start_addr = SIMPLE_FLASH_KEY_ADDR;
            info->partition[0].size = PARTITION_SIZE;
            break;
#if defined(KV_PROTECTED_ENABLE) && (KV_PROTECTED_ENABLE==1)
       case TUYA_FLASH_TYPE_KV_PROTECT:
            info->partition_num = 1;
            info->partition[0].block_size = PARTITION_SIZE;
            info->partition[0].start_addr = SIMPLE_FLASH_KV_PROTECTED_START;
            info->partition[0].size = SIMPLE_FLASH_KV_PROTECTED_SIZE;
            break;
#endif
       case TUYA_FLASH_TYPE_RCD:
            info->partition_num = 1;
            info->partition[0].block_size = PARTITION_SIZE;
            info->partition[0].start_addr = RCD_FILE_START;
            info->partition[0].size = RCD_FILE_SIZE;
            break;

        default:
            return OPRT_INVALID_PARM;
            break;
    }

    return OPRT_OK;
}


/**
* @brief lock flash
*
* @param[in] addr: lock begin addr
* @param[in] size: lock area size
*
* @note This API is used for lock flash.
*
* @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
*/
OPERATE_RET tkl_flash_lock(uint32_t addr, uint32_t size)
{
    return OPRT_NOT_SUPPORTED;
}

/**
* @brief unlock flash
*
* @param[in] addr: unlock begin addr
* @param[in] size: unlock area size
*
* @note This API is used for unlock flash.
*
* @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
*/
OPERATE_RET tkl_flash_unlock(uint32_t addr, uint32_t size)
{
    return OPRT_NOT_SUPPORTED;
}

/**
* @brief tuya_iot_license_read
*
* @param[in] license: iot license struct pointer
*
* @note This API is used for read license .
*
* @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
*/
int tuya_iot_license_read(tuya_iot_license_t *license)
{
    const char *path;
    struct stat file_stat;
    char content[256];
    size_t used = 0;
    int fd;
    int open_flags = O_RDONLY;

    if (license == NULL) {
        return OPRT_INVALID_PARM;
    }

    path = getenv("TUYA_LICENSE_FILE");
    if (path == NULL || path[0] == '\0') {
        path = TUYA_LICENSE_FILE_PATH;
    }

#ifdef O_CLOEXEC
    open_flags |= O_CLOEXEC;
#endif
#ifdef O_NOFOLLOW
    open_flags |= O_NOFOLLOW;
#endif
    fd = open(path, open_flags);
    if (fd < 0) {
        return OPRT_FILE_OPEN_FAILED;
    }

    if (fstat(fd, &file_stat) != 0 || !S_ISREG(file_stat.st_mode) ||
        file_stat.st_uid != 0 ||
        (file_stat.st_mode & (S_IRWXG | S_IRWXO)) != 0) {
        close(fd);
        return OPRT_COM_ERROR;
    }

    while (used < sizeof(content) - 1) {
        ssize_t count = read(fd, content + used, sizeof(content) - 1 - used);
        if (count < 0) {
            if (errno == EINTR) {
                continue;
            }
            close(fd);
            return OPRT_FILE_READ_FAILED;
        }
        if (count == 0) {
            break;
        }
        used += (size_t)count;
    }

    if (used == sizeof(content) - 1) {
        char extra;
        if (read(fd, &extra, 1) != 0) {
            close(fd);
            return OPRT_COM_ERROR;
        }
    }
    close(fd);

    content[used] = '\0';
    memset(s_license_uuid, 0, sizeof(s_license_uuid));
    memset(s_license_authkey, 0, sizeof(s_license_authkey));
    return __license_parse(content, license);
}
