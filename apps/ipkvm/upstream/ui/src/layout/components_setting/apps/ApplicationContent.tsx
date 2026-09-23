import { ChangeEvent, useCallback, useEffect, useRef, useState } from "react";
import { Alert, Button, Empty, Input, Popconfirm, Select, Spin, Tag } from "antd";
import { useReactAt } from "i18n-auto-extractor/react";

import { SettingsPageHeader } from "@components/Settings/SettingsPageheader";
import { SettingsPageLayout } from "@components/Settings/SettingsPageLayout";
import { DEVICE_API } from "@/ui.config";
import notifications from "@/notifications";

interface InstalledApp {
  id: string;
  version: string;
}

interface AppListResponse {
  apps: InstalledApp[];
}

interface PublisherKey {
  name: string;
  fingerprint: string;
}

interface PublisherKeyListResponse {
  keys: PublisherKey[];
}

const MAX_PACKAGE_BYTES = 32 * 1024 * 1024;
const MAX_PUBLIC_KEY_BYTES = 16 * 1024;

async function responseError(response: Response): Promise<string> {
  try {
    const payload = (await response.json()) as { error?: string };
    if (payload.error) return payload.error;
  } catch {
    // The generic status message below is safer than exposing an HTML body.
  }
  return `Request failed (${response.status})`;
}

export default function ApplicationContent() {
  const { $at } = useReactAt();
  const fileInput = useRef<HTMLInputElement>(null);
  const keyFileInput = useRef<HTMLInputElement>(null);
  const [apps, setApps] = useState<InstalledApp[]>([]);
  const [keys, setKeys] = useState<PublisherKey[]>([]);
  const [selectedFile, setSelectedFile] = useState<File | null>(null);
  const [keyName, setKeyName] = useState("");
  const [selectedKey, setSelectedKey] = useState<File | null>(null);
  const [mode, setMode] = useState("upgrade");
  const [loading, setLoading] = useState(true);
  const [installing, setInstalling] = useState(false);
  const [addingKey, setAddingKey] = useState(false);
  const [removing, setRemoving] = useState<string | null>(null);
  const [removingKey, setRemovingKey] = useState<string | null>(null);

  const refresh = useCallback(async () => {
    setLoading(true);
    try {
      const [appsResponse, keysResponse] = await Promise.all([
        fetch(`${DEVICE_API}/api/apps`, { credentials: "include" }),
        fetch(`${DEVICE_API}/api/apps/keys`, { credentials: "include" }),
      ]);
      if (!appsResponse.ok) throw new Error(await responseError(appsResponse));
      if (!keysResponse.ok) throw new Error(await responseError(keysResponse));
      const appPayload = (await appsResponse.json()) as AppListResponse;
      const keyPayload = (await keysResponse.json()) as PublisherKeyListResponse;
      setApps(Array.isArray(appPayload.apps) ? appPayload.apps : []);
      setKeys(Array.isArray(keyPayload.keys) ? keyPayload.keys : []);
    } catch (error) {
      notifications.error(
        error instanceof Error ? error.message : $at("Cannot load applications"),
      );
    } finally {
      setLoading(false);
    }
  }, [$at]);

  useEffect(() => {
    void refresh();
  }, [refresh]);

  const selectPackage = (event: ChangeEvent<HTMLInputElement>) => {
    const file = event.target.files?.[0] || null;
    if (file && file.size > MAX_PACKAGE_BYTES) {
      notifications.error($at("Application packages must not exceed 32 MiB"));
      event.target.value = "";
      setSelectedFile(null);
      return;
    }
    setSelectedFile(file);
  };

  const selectKey = (event: ChangeEvent<HTMLInputElement>) => {
    const file = event.target.files?.[0] || null;
    if (file && file.size > MAX_PUBLIC_KEY_BYTES) {
      notifications.error($at("Publisher public keys must not exceed 16 KiB"));
      event.target.value = "";
      setSelectedKey(null);
      return;
    }
    setSelectedKey(file);
  };

  const addKey = async () => {
    const normalizedName = keyName.trim();
    if (!/^[A-Za-z0-9][A-Za-z0-9._-]{0,63}$/.test(normalizedName)) {
      notifications.error($at("Enter a valid publisher key name"));
      return;
    }
    if (!selectedKey) {
      notifications.error($at("Select a PEM public key first"));
      return;
    }
    setAddingKey(true);
    try {
      const form = new FormData();
      form.append("key", selectedKey, selectedKey.name);
      const response = await fetch(
        `${DEVICE_API}/api/apps/keys?name=${encodeURIComponent(normalizedName)}`,
        {
          method: "POST",
          credentials: "include",
          body: form,
        },
      );
      if (!response.ok) throw new Error(await responseError(response));
      notifications.success($at("Publisher key added"));
      setKeyName("");
      setSelectedKey(null);
      if (keyFileInput.current) keyFileInput.current.value = "";
      await refresh();
    } catch (error) {
      notifications.error(
        error instanceof Error ? error.message : $at("Publisher key add failed"),
      );
    } finally {
      setAddingKey(false);
    }
  };

  const removeKey = async (name: string) => {
    setRemovingKey(name);
    try {
      const response = await fetch(
        `${DEVICE_API}/api/apps/keys/${encodeURIComponent(name)}`,
        {
          method: "DELETE",
          credentials: "include",
        },
      );
      if (!response.ok) throw new Error(await responseError(response));
      notifications.success($at("Publisher key removed"));
      await refresh();
    } catch (error) {
      notifications.error(
        error instanceof Error ? error.message : $at("Publisher key removal failed"),
      );
    } finally {
      setRemovingKey(null);
    }
  };

  const install = async () => {
    if (!selectedFile) {
      notifications.error($at("Select an .aitapp package first"));
      return;
    }
    setInstalling(true);
    try {
      const form = new FormData();
      form.append("package", selectedFile, selectedFile.name);
      const response = await fetch(
        `${DEVICE_API}/api/apps/install?mode=${encodeURIComponent(mode)}`,
        {
          method: "POST",
          credentials: "include",
          body: form,
        },
      );
      if (!response.ok) throw new Error(await responseError(response));
      notifications.success($at("Application installed"));
      setSelectedFile(null);
      if (fileInput.current) fileInput.current.value = "";
      await refresh();
    } catch (error) {
      notifications.error(
        error instanceof Error ? error.message : $at("Application install failed"),
        { duration: 5000 },
      );
    } finally {
      setInstalling(false);
    }
  };

  const remove = async (appID: string) => {
    setRemoving(appID);
    try {
      const response = await fetch(
        `${DEVICE_API}/api/apps/${encodeURIComponent(appID)}`,
        {
          method: "DELETE",
          credentials: "include",
        },
      );
      if (!response.ok) throw new Error(await responseError(response));
      notifications.success($at("Application removed"));
      await refresh();
    } catch (error) {
      notifications.error(
        error instanceof Error ? error.message : $at("Application removal failed"),
      );
    } finally {
      setRemoving(null);
    }
  };

  return (
    <SettingsPageLayout>
      <SettingsPageHeader
        title={$at("Applications")}
        description={$at("Upload and manage signed applications directly from this browser")}
      />

      <Alert
        type="info"
        showIcon
        message={$at("Signed packages only")}
        description={$at("The device verifies the publisher signature, package contents, permissions, and version policy before installation. ADB is not used.")}
      />

      <section className="rounded-2xl border border-blue-200 bg-gradient-to-br from-blue-50 to-white p-4 dark:border-blue-900 dark:from-blue-950/50 dark:to-slate-900/30">
        <div className="mb-4 flex flex-wrap items-end justify-between gap-2">
          <div>
            <h3 className="font-semibold text-slate-900 dark:text-white">
              {$at("2048 example: build, trust, upload, run")}
            </h3>
            <p className="mt-1 text-sm text-slate-600 dark:text-slate-300">
              {$at("A complete embedded user UI example is included in the repository. It always stays inside the User Applications container.")}
            </p>
          </div>
          <Tag color="blue">platform/app-sdk/examples/2048</Tag>
        </div>
        <ol className="grid gap-3 text-sm md:grid-cols-2 xl:grid-cols-4">
          {[
            ["1", $at("Create signing keys"), "openssl genpkey -algorithm RSA -pkeyopt rsa_keygen_bits:3072 -out developer.key\nopenssl pkey -in developer.key -pubout -out developer.pem"],
            ["2", $at("Compile the example"), "python3 tools/aitapp/aitapp.py compile platform/app-sdk/examples/2048 --sdk ~/A133-Tina5.0-v0.9"],
            ["3", $at("Build the package"), "python3 tools/aitapp/aitapp.py build platform/app-sdk/examples/2048 --key developer.key -o game2048.aitapp"],
            ["4", $at("Install and open"), $at("Upload developer.pem below, then upload game2048.aitapp. Open “User Applications” on the device desktop.")],
          ].map(([number, title, command]) => (
            <li
              key={number}
              className="min-w-0 rounded-xl border border-blue-100 bg-white/90 p-3 dark:border-blue-900/70 dark:bg-slate-950/70"
            >
              <div className="mb-2 flex items-center gap-2">
                <span className="flex size-6 shrink-0 items-center justify-center rounded-full bg-blue-600 text-xs font-bold text-white">
                  {number}
                </span>
                <span className="font-medium text-slate-900 dark:text-white">{title}</span>
              </div>
              <pre className="overflow-x-auto whitespace-pre-wrap break-all rounded-lg bg-slate-950 p-2 text-[10px] leading-relaxed text-slate-100">
                {command}
              </pre>
            </li>
          ))}
        </ol>
      </section>

      <section className="space-y-4 rounded-xl border border-slate-200 p-4 dark:border-slate-700">
        <h3 className="font-semibold text-slate-900 dark:text-white">
          {$at("Trusted publishers")}
        </h3>
        <p className="text-sm text-slate-600 dark:text-slate-300">
          {$at("Add only public keys from publishers you trust. Private signing keys must never be uploaded.")}
        </p>
        <div className="grid gap-3 sm:grid-cols-2">
          <Input
            value={keyName}
            maxLength={64}
            disabled={addingKey}
            placeholder={$at("Publisher key name")}
            onChange={event => setKeyName(event.target.value)}
            data-testid="app-key-name"
          />
          <input
            ref={keyFileInput}
            type="file"
            accept=".pem,application/x-pem-file,text/plain"
            onChange={selectKey}
            disabled={addingKey}
            className="block w-full text-sm text-slate-600 file:mr-3 file:rounded-md file:border-0 file:bg-slate-600 file:px-3 file:py-2 file:text-white hover:file:bg-slate-500 dark:text-slate-300"
            data-testid="app-key-file"
          />
        </div>
        <Button
          type="primary"
          loading={addingKey}
          disabled={!selectedKey || !keyName.trim()}
          onClick={addKey}
          data-testid="app-key-add"
        >
          {$at("Add publisher key")}
        </Button>
        {keys.length > 0 && (
          <div className="space-y-2">
            {keys.map(key => (
              <div
                key={key.name}
                className="flex flex-wrap items-center justify-between gap-3 rounded-lg bg-slate-50 p-3 dark:bg-slate-900/50"
              >
                <div className="min-w-0">
                  <p className="text-sm font-medium text-slate-900 dark:text-white">
                    {key.name}
                  </p>
                  <p className="break-all font-mono text-[10px] text-slate-500 dark:text-slate-400">
                    SHA-256: {key.fingerprint}
                  </p>
                </div>
                <Popconfirm
                  title={$at("Remove this publisher key?")}
                  description={$at("Keys used by installed applications cannot be removed.")}
                  okText={$at("Remove")}
                  cancelText={$at("Cancel")}
                  onConfirm={() => removeKey(key.name)}
                >
                  <Button danger size="small" loading={removingKey === key.name}>
                    {$at("Remove")}
                  </Button>
                </Popconfirm>
              </div>
            ))}
          </div>
        )}
      </section>

      <section className="space-y-4 rounded-xl border border-slate-200 p-4 dark:border-slate-700">
        <h3 className="font-semibold text-slate-900 dark:text-white">
          {$at("Install an application")}
        </h3>
        <input
          ref={fileInput}
          type="file"
          accept=".aitapp,application/gzip"
          onChange={selectPackage}
          disabled={installing}
          className="block w-full text-sm text-slate-600 file:mr-3 file:rounded-md file:border-0 file:bg-blue-600 file:px-3 file:py-2 file:text-white hover:file:bg-blue-500 dark:text-slate-300"
          data-testid="app-package"
        />
        <div className="flex flex-wrap items-center gap-3">
          <Select
            value={mode}
            disabled={installing}
            onChange={setMode}
            className="min-w-56"
            options={[
              { value: "upgrade", label: $at("Normal upgrade") },
              { value: "replace", label: $at("Replace same version") },
              { value: "downgrade", label: $at("Allow version downgrade") },
            ]}
          />
          <Button
            type="primary"
            loading={installing}
            disabled={!selectedFile}
            onClick={install}
            data-testid="app-install"
          >
            {$at("Upload and install")}
          </Button>
        </div>
        {selectedFile && (
          <p className="break-all text-xs text-slate-500 dark:text-slate-400">
            {selectedFile.name} · {(selectedFile.size / 1024).toFixed(1)} KiB
          </p>
        )}
      </section>

      <section className="space-y-3 rounded-xl border border-slate-200 p-4 dark:border-slate-700">
        <div className="flex flex-wrap items-center justify-between gap-2">
          <h3 className="font-semibold text-slate-900 dark:text-white">
            {$at("Installed applications")}
          </h3>
          <Button size="small" disabled={loading} onClick={() => void refresh()}>
            {$at("Refresh")}
          </Button>
        </div>
        {loading ? (
          <div className="flex justify-center py-8">
            <Spin />
          </div>
        ) : apps.length === 0 ? (
          <Empty
            image={Empty.PRESENTED_IMAGE_SIMPLE}
            description={$at("No applications installed")}
          />
        ) : (
          <div className="space-y-2">
            {apps.map(app => (
              <div
                key={app.id}
                className="flex flex-wrap items-center justify-between gap-3 rounded-lg bg-slate-50 p-3 dark:bg-slate-900/50"
              >
                <div className="min-w-0">
                  <p className="break-all text-sm font-medium text-slate-900 dark:text-white">
                    {app.id}
                  </p>
                  <Tag className="mt-1">{app.version}</Tag>
                </div>
                <Popconfirm
                  title={$at("Remove this application?")}
                  description={$at("Application files and private data will be deleted.")}
                  okText={$at("Remove")}
                  cancelText={$at("Cancel")}
                  onConfirm={() => remove(app.id)}
                >
                  <Button danger loading={removing === app.id}>
                    {$at("Remove")}
                  </Button>
                </Popconfirm>
              </div>
            ))}
          </div>
        )}
      </section>
    </SettingsPageLayout>
  );
}
