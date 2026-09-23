#include "aitvbox/app.h"

#include <string.h>

int main(int argc, char **argv)
{
    const char *action = aitvbox_app_action(argc, argv);
    if (!action)
        return 2;
    if (!strcmp(action, "status")) {
        char result[2048];
        if (!aitvbox_capability_call(
                "hardware.capabilities", "{}", result, sizeof(result)))
            return aitvbox_app_reply(true, "Hardware status is available.");
        return aitvbox_app_reply(false, "Hardware status is unavailable.");
    }
    return aitvbox_app_reply(false, "Unknown action.");
}
