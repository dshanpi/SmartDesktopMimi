import { useEffect, useRef, useState } from "react";
import { Button, Alert, Switch, Select } from "antd";
import { useReactAt } from "i18n-auto-extractor/react";

interface UpdatePolicy {
  schema_version: number;
  channel: "stable" | "testing";
  auto_download: boolean;
  install_when_idle: boolean;
}

interface Status {
  policy?: UpdatePolicy;
  current_version?: string;
  phase: string;
  busy: boolean;
  error: string;
  channel: string;
  downloaded: boolean;
  download_bytes?: number;
  total_bytes?: number;
  candidate?: { manifest: { version: string } };
  state?: { history: { time: string; version: string; result: string; reason: string }[] };
}

export default function DesktopUpdate() {
  const { $at } = useReactAt();
  const [status, setStatus] = useState<Status | null>(null);
  const [statusError, setStatusError] = useState("");
  const [actionError, setActionError] = useState("");
  const [pending, setPending] = useState(false);
  const pendingRef = useRef(false);
  const revision = useRef(0);
  useEffect(() => {
    const controller = new AbortController();
    let timer: ReturnType<typeof setTimeout>;
    const poll = async () => {
      if (pendingRef.current) {
        timer = setTimeout(poll, 2000);
        return;
      }
      const requestRevision = revision.current;
      try {
        const response = await fetch("/api/desktop-update/status", { signal: controller.signal });
        if (!response.headers.get("content-type")?.includes("application/json")) {
          throw new Error($at("Desktop updater unavailable"));
        }
        const data = await response.json();
        if (!response.ok) throw new Error(data.error || $at("Desktop updater unavailable"));
        if (typeof data.phase !== "string" || typeof data.busy !== "boolean") {
          throw new Error($at("Desktop updater unavailable"));
        }
        if (!controller.signal.aborted && requestRevision === revision.current) {
          setStatus(data); setStatusError("");
        }
      } catch (err) {
        if (!controller.signal.aborted && requestRevision === revision.current) {
          setStatusError(err instanceof Error ? err.message : $at("Desktop updater unavailable")); setStatus(null);
        }
      } finally {
        if (!controller.signal.aborted) timer = setTimeout(poll, 2000);
      }
    };
    void poll();
    return () => { controller.abort(); clearTimeout(timer); };
  }, [$at]);
  const beginAction = () => {
    if (pendingRef.current) return false;
    pendingRef.current = true;
    revision.current += 1;
    setPending(true);
    setActionError("");
    return true;
  };
  const finishAction = () => {
    pendingRef.current = false;
    setPending(false);
  };
  const responseError = async (response: Response) => {
    const text = await response.text();
    try {
      const data = JSON.parse(text);
      if (typeof data.error === "string") return data.error;
    } catch { /* The updater also returns plain-text errors. */ }
    return text || $at("Desktop updater unavailable");
  };
  const action = async (operation: string) => {
    if (!beginAction()) return;
    try {
      const response = await fetch(`/api/desktop-update/${operation}`, { method: "POST" });
      if (!response.ok) throw new Error(await responseError(response));
      // An accepted operation is active until a fresh status says otherwise.
      if (operation !== "cancel") setStatus(current => current ? { ...current, busy: true, phase: operation } : null);
    } catch (err) { setActionError(err instanceof Error ? err.message : $at("Desktop updater unavailable")); }
    finally { finishAction(); }
  };
  const savePolicy = async (patch: Partial<UpdatePolicy>) => {
    if (!status?.policy || !beginAction()) return;
    const policy = { ...status.policy, ...patch };
    try {
      const response = await fetch("/api/desktop-update/policy", {
        method: "PUT", headers: { "Content-Type": "application/json" }, body: JSON.stringify(policy),
      });
      if (!response.ok) throw new Error(await responseError(response));
      setStatus(current => current ? { ...current, policy, channel: policy.channel,
        ...(current.channel !== policy.channel ? { candidate: undefined, downloaded: false } : {}) } : null);
    } catch (err) { setActionError(err instanceof Error ? err.message : $at("Desktop updater unavailable")); }
    finally { finishAction(); }
  };
  return <section className="space-y-3" aria-label={$at("Desktop application update")}>
    <h3 className="font-semibold">{$at("Desktop application update")}</h3>
    <p>{$at("Updates desktop pages and resources. System firmware is managed separately.")}</p>
    {statusError && <Alert type="warning" title={statusError} />}
    {actionError && <Alert type="error" title={actionError} closable onClose={() => setActionError("")} />}
    {status?.error && <Alert type="error" title={status.error} />}
    {status && <p>{status.current_version} · {status.channel} · {status.phase} {status.candidate?.manifest.version}</p>}
    {status?.phase === "download" && <progress className="w-full" aria-label={$at("Download")} value={status.download_bytes || 0} max={status.total_bytes || 1} />}
    <div className="flex flex-wrap gap-2">
      <Button disabled={!status || status.busy || pending} onClick={() => action("check")}>{$at("Check for updates")}</Button>
      <Button disabled={!status?.candidate || status.busy || pending} onClick={() => action("download")}>{$at("Download")}</Button>
      <Button disabled={!status?.downloaded || status.busy || pending} onClick={() => action("install")}>{$at("Install and restart desktop")}</Button>
      <Button disabled={!status?.busy || status.phase === "install" || pending} onClick={() => action("cancel")}>{$at("Cancel download")}</Button>
    </div>
    {status?.policy && <div className="space-y-3">
      <div className="flex flex-wrap items-center justify-between gap-2">
        <span>{$at("Update channel")}</span>
        <Select aria-label={$at("Update channel")} value={status.policy.channel} disabled={status.busy || pending}
          options={[{ value: "stable", label: $at("Stable") }, { value: "testing", label: $at("Testing") }]}
          onChange={channel => void savePolicy({ channel })} />
      </div>
      <div className="flex items-center justify-between gap-2">
        <span>{$at("Automatically download updates")}</span>
        <Switch aria-label={$at("Automatically download updates")} checked={status.policy.auto_download}
          disabled={status.busy || pending} onChange={auto_download => void savePolicy({ auto_download })} />
      </div>
      <div className="flex items-center justify-between gap-2">
        <span>{$at("Install when the device is idle")}</span>
        <Switch aria-label={$at("Install when the device is idle")} checked={status.policy.install_when_idle}
          disabled={status.busy || pending} onChange={install_when_idle => void savePolicy({ install_when_idle })} />
      </div>
      <p>{$at("Idle installation restarts the desktop after voice, media and device control tasks finish.")}</p>
    </div>}
    {!!status?.state?.history?.length && <details><summary>{$at("Update history")}</summary>
      <ul>{status.state.history.slice(-10).reverse().map((entry, index) => <li key={`${entry.time}-${index}`}>{entry.time} · {entry.version} · {entry.result} {entry.reason}</li>)}</ul>
    </details>}
  </section>;
}
