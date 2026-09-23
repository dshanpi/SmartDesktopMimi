/* Public-build cloud provider.
 *
 * The 100ask SDK is an optional private dependency.  Keep the backend ABI
 * stable when it is absent and report an explicit, non-retryable state to the
 * UI instead of pretending that the network is offline.
 */

#include "service_cloud.h"

#include <stdbool.h>
#include <string.h>

#include "backend_types.h"
#include "../middleware/middleware.h"

static cloud_status_t g_status;

static void publish_unavailable(void)
{
    mw_publish(TOPIC_CLOUD_STATUS, &g_status, sizeof(g_status),
               MW_DIR_BACKEND_TO_UI);
}

void service_cloud_init(void)
{
    memset(&g_status, 0, sizeof(g_status));
    g_status.state = CLOUD_STATE_UNAVAILABLE;
    publish_unavailable();
}

void service_cloud_update(void)
{
}

void service_cloud_deinit(void)
{
}

void service_cloud_clear_ota_pending(void)
{
    publish_unavailable();
}

void service_cloud_send_status(void)
{
    publish_unavailable();
}

void service_cloud_refresh_bind_token(void)
{
    publish_unavailable();
}

void service_cloud_set_tuya_license_ready(bool ready)
{
    (void)ready;
}
