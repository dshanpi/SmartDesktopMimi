import api from "@/api";
import { DEVICE_API } from "@/ui.config";

let redirectInProgress = false;

/**
 * A WebSocket upgrade does not expose its HTTP 401 status to browser code.
 * Confirm the session against a protected HTTP endpoint before redirecting,
 * so ordinary service/network interruptions keep using automatic reconnect.
 */
export async function redirectToLoginIfSessionExpired(): Promise<boolean> {
  if (redirectInProgress) return true;

  try {
    const response = await api.GET(`${DEVICE_API}/device`);
    if (response.status !== 401) return false;

    redirectInProgress = true;
    window.location.replace("/login-local");
    return true;
  } catch {
    // The service can be temporarily unavailable during an upgrade/restart.
    // The next WebSocket close will probe again after connectivity returns.
    return false;
  }
}
