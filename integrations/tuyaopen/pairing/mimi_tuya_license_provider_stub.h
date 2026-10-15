/* Public implementation cannot read old factory credentials. */
#ifndef MIMI_TUYA_LICENSE_PROVIDER_H
#define MIMI_TUYA_LICENSE_PROVIDER_H
#include <stdbool.h>
static bool tuya_license_file_valid(void) { return false; }
#endif
