import { useCallback, useEffect, useRef, useState } from "react";
import { Alert, Button, Input, InputNumber, Tag } from "antd";
import { useReactAt } from "i18n-auto-extractor/react";

import { SettingsPageHeader } from "@components/Settings/SettingsPageheader";
import { SettingsPageLayout } from "@components/Settings/SettingsPageLayout";
import { useAgentControlStore } from "@/hooks/stores";
import { JsonRpcResponse, sendHttpRpc } from "@/hooks/useJsonRpc";
import notifications from "@/notifications";

interface AgentStatus {
  ok: boolean;
  message?: string;
  state: "idle" | "running" | "stopping" | "succeeded" | "failed";
  configured: boolean;
  keyConfigured: boolean;
  providerConfigured: boolean;
  endpoint?: string;
  model?: string;
  detail?: string;
  history?: AgentHistoryEntry[];
}

interface AgentHistoryEntry {
  role: "user" | "assistant" | "tool" | "system";
  type: string;
  step?: number;
  message: string;
}

const DEFAULT_ENDPOINT = "https://api.openai.com/v1/chat/completions";
const DEFAULT_MODEL = "gpt-4o-mini";
const VOLCENGINE_CODING_MODEL = "doubao-seed-2.0-code";

function rpcResult(response: JsonRpcResponse): AgentStatus {
  if ("error" in response) {
    throw new Error(response.error.data || response.error.message);
  }
  return response.result as AgentStatus;
}

function delay(milliseconds: number) {
  return new Promise(resolve => window.setTimeout(resolve, milliseconds));
}

export default function AgentContent() {
  const { $at } = useReactAt();
  const agentControlActive = useAgentControlStore(state => state.active);
  const setAgentControlActive = useAgentControlStore(state => state.setActive);
  const [status, setStatus] = useState<AgentStatus | null>(null);
  const [endpoint, setEndpoint] = useState(DEFAULT_ENDPOINT);
  const [model, setModel] = useState(DEFAULT_MODEL);
  const [apiKey, setApiKey] = useState("");
  const [task, setTask] = useState("");
  const [maxSteps, setMaxSteps] = useState(20);
  const [busy, setBusy] = useState(false);
  const historyRef = useRef<HTMLDivElement>(null);

  const refresh = useCallback(async () => {
    try {
      const next = rpcResult(await sendHttpRpc("getAgentStatus"));
      setStatus(next);
      if (next.endpoint) setEndpoint(next.endpoint);
      if (next.model) setModel(next.model);
      if (next.state === "running" || next.state === "stopping") {
        setAgentControlActive(true);
      }
      return next;
    } catch (error) {
      notifications.error(
        error instanceof Error ? error.message : $at("Computer control service is unavailable"),
      );
      return null;
    }
  }, [$at, setAgentControlActive]);

  useEffect(() => {
    void refresh();
  }, [refresh]);

  useEffect(() => {
    if (!agentControlActive) return;
    const timer = window.setInterval(() => {
      void refresh();
    }, 1000);
    return () => window.clearInterval(timer);
  }, [agentControlActive, refresh]);

  useEffect(() => {
    if (!historyRef.current) return;
    historyRef.current.scrollTop = historyRef.current.scrollHeight;
  }, [status?.history?.length]);

  const saveProvider = async () => {
    if (!endpoint.startsWith("https://") || !model.trim()) {
      notifications.error($at("Enter an HTTPS Base URL or endpoint and a model name"));
      return;
    }
    if (model.toLowerCase().includes("embedding")) {
      notifications.error(
        endpoint.includes("/api/coding/")
          ? $at("This model is embedding-only. For Volcengine Coding Plan use doubao-seed-2.0-code.")
          : $at("This model is embedding-only. Choose a vision-capable chat/completions model."),
      );
      return;
    }
    setBusy(true);
    try {
      rpcResult(await sendHttpRpc("configureAgent", {
        endpoint: endpoint.trim(),
        model: model.trim(),
      }));
      notifications.success($at("Model provider saved"));
      await refresh();
    } catch (error) {
      notifications.error(error instanceof Error ? error.message : $at("Save failed"));
    } finally {
      setBusy(false);
    }
  };

  const saveKey = async () => {
    if (!/^[!-~]{12,4096}$/.test(apiKey)) {
      notifications.error($at("The API key must be 12 to 4096 printable characters"));
      return;
    }
    setBusy(true);
    try {
      rpcResult(await sendHttpRpc("setAgentKey", { key: apiKey }));
      setApiKey("");
      notifications.success($at("API key saved securely"));
      await refresh();
    } catch (error) {
      notifications.error(error instanceof Error ? error.message : $at("Save failed"));
    } finally {
      setBusy(false);
    }
  };

  const clearKey = async () => {
    setBusy(true);
    try {
      rpcResult(await sendHttpRpc("clearAgentKey"));
      notifications.success($at("API key cleared"));
      await refresh();
    } catch (error) {
      notifications.error(error instanceof Error ? error.message : $at("Clear failed"));
    } finally {
      setBusy(false);
    }
  };

  const startTask = async () => {
    const normalizedTask = task.trim();
    if (!normalizedTask || normalizedTask.length > 512) {
      notifications.error($at("Enter a task between 1 and 512 characters"));
      return;
    }
    if (!status?.configured) {
      notifications.error($at("Save the provider and API key first"));
      return;
    }

    setBusy(true);
    setAgentControlActive(true);
    try {
      const next = rpcResult(await sendHttpRpc("startAgent", {
        task: normalizedTask,
        maxSteps,
      }));
      setStatus(next);
      notifications.success($at("Computer control task started"));
      await refresh();
    } catch (error) {
      await refresh();
      setAgentControlActive(false);
      notifications.error(error instanceof Error ? error.message : $at("Start failed"));
    } finally {
      setBusy(false);
    }
  };

  const stopTask = async () => {
    setBusy(true);
    try {
      rpcResult(await sendHttpRpc("stopAgent"));
      notifications.success($at("Stop requested"));
      await refresh();
    } catch (error) {
      notifications.error(error instanceof Error ? error.message : $at("Stop failed"));
    } finally {
      setBusy(false);
    }
  };

  const resumeLiveVideo = async () => {
    if (status?.state === "running" || status?.state === "stopping") {
      await stopTask();
      for (let attempt = 0; attempt < 20; attempt++) {
        const next = await refresh();
        if (next && next.state !== "running" && next.state !== "stopping") break;
        await delay(100);
      }
    } else {
      // Release the agent's reference to the shared capture pipeline even
      // after a completed task. Browser video remains connected throughout.
      try {
        rpcResult(await sendHttpRpc("stopAgent"));
      } catch (error) {
        notifications.error(
          error instanceof Error ? error.message : $at("Resume failed"),
        );
        return;
      }
    }
    setAgentControlActive(false);
  };

  const stateColor =
    status?.state === "running" ? "processing" :
    status?.state === "succeeded" ? "success" :
    status?.state === "failed" ? "error" : "default";

  return (
    <SettingsPageLayout>
      <SettingsPageHeader
        title={$at("HDMI + HID Computer Control")}
        description={$at("Configure and run visual computer tasks directly from the IPKVM browser")}
      />

      <Alert
        type="info"
        showIcon
        message={$at("How it works")}
        description={$at("The live 1080p HDMI stream remains connected. The AI requests individual analysis snapshots from the shared capture pipeline and sends validated keyboard and mouse actions through USB HID.")}
      />

      <div className="grid gap-4 xl:grid-cols-[minmax(0,0.8fr)_minmax(0,1.2fr)]">
      <section className="space-y-3 rounded-2xl border border-slate-200 bg-white p-4 dark:border-slate-700 dark:bg-slate-900/30">
        <div className="flex flex-wrap items-center justify-between gap-2">
          <h3 className="font-semibold text-slate-900 dark:text-white">
            {$at("Service status")}
          </h3>
          <Tag color={stateColor} data-testid="agent-state">
            {status?.state || $at("loading")}
          </Tag>
        </div>
        <div className="grid gap-2 text-sm text-slate-600 sm:grid-cols-2 dark:text-slate-300">
          <span>
            {$at("Model provider")}: {status?.providerConfigured ? $at("configured") : $at("missing")}
          </span>
          <span>
            {$at("API key")}: {status?.keyConfigured ? $at("configured") : $at("missing")}
          </span>
        </div>
        {status?.detail && (
          <pre
            className="max-h-40 overflow-auto whitespace-pre-wrap break-words rounded-lg bg-slate-100 p-3 text-xs dark:bg-slate-900"
            data-testid="agent-detail"
          >
            {status.detail}
          </pre>
        )}
      </section>

      <section className="flex min-h-72 flex-col overflow-hidden rounded-2xl border border-slate-200 bg-slate-50 dark:border-slate-700 dark:bg-slate-950/60">
        <div className="flex items-center justify-between border-b border-slate-200 px-4 py-3 dark:border-slate-800">
          <div>
            <h3 className="font-semibold text-slate-900 dark:text-white">
              {$at("Conversation and execution history")}
            </h3>
            <p className="text-xs text-slate-500 dark:text-slate-400">
              {$at("The model's visible-screen observations, action reasons, commands, and HID results appear here in order.")}
            </p>
          </div>
          <Tag>{status?.history?.length || 0}</Tag>
        </div>
        <div
          ref={historyRef}
          className="min-h-0 flex-1 space-y-3 overflow-y-auto p-4"
          data-testid="agent-history"
        >
          {!status?.history?.length ? (
            <div className="flex h-full min-h-48 items-center justify-center text-center text-sm text-slate-500 dark:text-slate-400">
              {$at("No task history yet. Enter a task below to begin.")}
            </div>
          ) : (
            status.history.map((entry, index) => (
              <div
                key={`${index}-${entry.step || 0}-${entry.type}`}
                className={`flex ${entry.role === "user" ? "justify-end" : "justify-start"}`}
              >
                <div
                  className={`max-w-[88%] rounded-2xl px-3.5 py-2.5 text-sm shadow-sm ${
                    entry.role === "user"
                      ? "rounded-br-md bg-blue-600 text-white"
                      : entry.role === "assistant"
                        ? "rounded-bl-md border border-violet-200 bg-violet-50 text-violet-950 dark:border-violet-900 dark:bg-violet-950/50 dark:text-violet-100"
                        : entry.type === "error" || entry.type === "failed"
                          ? "rounded-bl-md border border-red-200 bg-red-50 text-red-900 dark:border-red-900 dark:bg-red-950/50 dark:text-red-100"
                          : "rounded-bl-md border border-slate-200 bg-white text-slate-700 dark:border-slate-700 dark:bg-slate-900 dark:text-slate-200"
                  }`}
                >
                  <div className="mb-1 flex items-center gap-2 text-[10px] font-semibold uppercase tracking-wide opacity-65">
                    <span>{entry.role}</span>
                    <span>· {entry.type}</span>
                    {entry.step ? <span>· step {entry.step}</span> : null}
                  </div>
                  <p className="whitespace-pre-wrap break-words">{entry.message}</p>
                </div>
              </div>
            ))
          )}
        </div>
      </section>
      </div>

      <section className="space-y-3 rounded-xl border border-slate-200 p-4 dark:border-slate-700">
        <h3 className="font-semibold text-slate-900 dark:text-white">
          {$at("Model provider")}
        </h3>
        <label className="block space-y-1">
          <span className="text-sm text-slate-600 dark:text-slate-300">
            {$at("HTTPS Base URL or chat-completions endpoint")}
          </span>
          <Input
            value={endpoint}
            maxLength={2048}
            onChange={event => setEndpoint(event.target.value)}
            data-testid="agent-endpoint"
          />
        </label>
        <label className="block space-y-1">
          <span className="text-sm text-slate-600 dark:text-slate-300">
            {$at("Model")}
          </span>
          <Input
            value={model}
            maxLength={2048}
            onChange={event => setModel(event.target.value)}
            data-testid="agent-model"
          />
        </label>
        {model.toLowerCase().includes("embedding") && (
          <Alert
            type="error"
            showIcon
            message={
              endpoint.includes("/api/coding/")
                ? $at("The selected model is embedding-only; API key permissions do not change its output type.")
                : $at("The selected model is embedding-only and cannot control HDMI.")
            }
            description={
              endpoint.includes("/api/coding/")
                ? $at("For Volcengine Coding Plan, select doubao-seed-2.0-code for HDMI image understanding.")
                : $at("Select a vision-capable chat/completions model.")
            }
            action={
              endpoint.includes("/api/coding/") ? (
                <Button size="small" onClick={() => setModel(VOLCENGINE_CODING_MODEL)}>
                  {$at("Use Coding Plan vision model")}
                </Button>
              ) : undefined
            }
          />
        )}
        <Button
          type="primary"
          loading={busy}
          disabled={model.toLowerCase().includes("embedding")}
          onClick={saveProvider}
        >
          {$at("Save provider")}
        </Button>
      </section>

      <section className="space-y-3 rounded-xl border border-slate-200 p-4 dark:border-slate-700">
        <h3 className="font-semibold text-slate-900 dark:text-white">
          {$at("API key")}
        </h3>
        <Input.Password
          value={apiKey}
          maxLength={4096}
          autoComplete="new-password"
          placeholder="sk-... / ark-..."
          onChange={event => setApiKey(event.target.value)}
          data-testid="agent-api-key"
        />
        <div className="flex flex-wrap gap-2">
          <Button type="primary" loading={busy} onClick={saveKey}>
            {$at("Save API key")}
          </Button>
          <Button danger disabled={!status?.keyConfigured || busy} onClick={clearKey}>
            {$at("Clear API key")}
          </Button>
        </div>
      </section>

      <section className="space-y-3 rounded-xl border border-slate-200 p-4 dark:border-slate-700">
        <h3 className="font-semibold text-slate-900 dark:text-white">
          {$at("Computer task")}
        </h3>
        <Input.TextArea
          value={task}
          maxLength={512}
          rows={4}
          showCount
          placeholder={$at("For example: open a terminal and show the operating system version")}
          onChange={event => setTask(event.target.value)}
          data-testid="agent-task"
        />
        <label className="flex flex-wrap items-center gap-3 text-sm text-slate-600 dark:text-slate-300">
          <span>{$at("Maximum steps")}</span>
          <InputNumber
            min={1}
            max={30}
            value={maxSteps}
            onChange={value => setMaxSteps(value || 20)}
          />
        </label>
        <div className="flex flex-wrap gap-2">
          <Button
            type="primary"
            loading={busy}
            disabled={status?.state === "running" || status?.state === "stopping"}
            onClick={startTask}
            data-testid="agent-start"
          >
            {$at("Start AI control")}
          </Button>
          <Button
            danger
            disabled={
              busy ||
              (status?.state !== "running" && status?.state !== "stopping")
            }
            onClick={stopTask}
            data-testid="agent-stop"
          >
            {$at("Emergency stop")}
          </Button>
          {agentControlActive && (
            <Button onClick={resumeLiveVideo} data-testid="agent-resume-video">
              {$at("Stop task and release AI control")}
            </Button>
          )}
        </div>
      </section>
    </SettingsPageLayout>
  );
}
