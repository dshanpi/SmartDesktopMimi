// Host-only browser regression: real component, mocked updater HTTP responses.
import { createServer } from "vite";
import { chromium, expect } from "@playwright/test";
import { fileURLToPath } from "node:url";
import { resolve } from "node:path";
import { mkdir, writeFile } from "node:fs/promises";

const root = fileURLToPath(new URL("../", import.meta.url));
const output = resolve(root, "../../../../output/optimization-20261012");
await mkdir(output, { recursive: true });
const server = await createServer({
  root, configFile: false, logLevel: "error",
  optimizeDeps: { entries: ["src/layout/components_setting/version/DesktopUpdate.tsx"] },
  server: { host: "127.0.0.1", port: 0 },
  plugins: [{
    name: "update-regression-fixture",
    resolveId(id) {
      if (id === "virtual:mimi-test" || id === "i18n-auto-extractor/react") return `\0${id}`;
    },
    load(id) {
      if (id === "\0i18n-auto-extractor/react") return "const translate = s => s; export const useReactAt = () => ({ $at: translate });";
      if (id === "\0virtual:mimi-test") return `
        import React from 'react';
        import {createRoot} from 'react-dom/client';
        import DesktopUpdate from '/src/layout/components_setting/version/DesktopUpdate.tsx';
        createRoot(document.getElementById('root')).render(React.createElement(DesktopUpdate));`;
    },
    configureServer(vite) {
      vite.middlewares.use((req, res, next) => {
        if (req.url !== "/") return next();
        res.setHeader("Content-Type", "text/html");
        res.end('<!doctype html><html><meta charset="utf-8"><div id="root"></div><script type="module" src="/@id/virtual:mimi-test"></script></html>');
      });
    },
  }],
});
await server.listen();
const baseURL = `http://127.0.0.1:${server.httpServer.address().port}`;
const browser = await chromium.launch({ headless: true });
const initial = () => ({
  phase: "idle", busy: false, error: "", channel: "stable", current_version: "1.0.0",
  downloaded: true, candidate: { manifest: { version: "1.1.0" } },
  policy: { schema_version: 1, channel: "stable", auto_download: false, install_when_idle: false },
});
const json = (route, body, status = 200) => route.fulfill({ status, contentType: "application/json", body: JSON.stringify(body) });
const results = [];
async function scenario(name, run) {
  const context = await browser.newContext({ viewport: { width: 390, height: 844 } });
  const page = await context.newPage();
  try {
    await run(page);
    results.push({ name, passed: true });
    console.log(`PASS ${name}`);
  } catch (error) {
    results.push({ name, passed: false, error: error.message });
    console.error(`FAIL ${name}: ${error.message}`);
    process.exitCode = 1;
  } finally { await context.close(); }
}
try {
  await scenario("action error survives successful polling", async page => {
    let polls = 0;
    await page.route("**/api/desktop-update/**", route => {
      if (route.request().method() === "GET") { polls++; return json(route, initial()); }
      return json(route, { error: "Storage check refused installation" }, 409);
    });
    await page.goto(baseURL);
    await page.getByRole("button", { name: "Check for updates", exact: true }).click();
    await expect(page.getByText("Storage check refused installation", { exact: false })).toBeVisible();
    const before = polls;
    await expect.poll(() => polls, { timeout: 6000 }).toBeGreaterThan(before);
    await expect(page.getByText("Storage check refused installation", { exact: false })).toBeVisible({ timeout: 500 });
    await page.screenshot({ path: resolve(output, "action-error-mobile.png"), fullPage: true });
  });
  await scenario("stale poll cannot overwrite saved policy", async page => {
    let polls = 0, held;
    await page.route("**/api/desktop-update/**", route => {
      if (route.request().method() === "PUT") return route.fulfill({ status: 204 });
      polls++;
      if (polls === 1) return json(route, initial());
      if (polls === 2) held = route;
      // Keep subsequent responses pending so an incorrect stale commit cannot
      // be hidden by another successful poll during the assertion.
    });
    await page.goto(baseURL);
    await expect.poll(() => Boolean(held), { timeout: 6000 }).toBe(true);
    const control = page.getByRole("switch", { name: "Automatically download updates" });
    await control.click();
    await expect(control).toBeChecked();
    await json(held, initial());
    await page.waitForTimeout(150);
    await expect(control).toBeChecked({ timeout: 500 });
  });
  await scenario("accepted install stays locked until fresh status", async page => {
    let accepted = false, held;
    await page.route("**/api/desktop-update/**", route => {
      if (route.request().method() === "POST") { accepted = true; return route.fulfill({ status: 202 }); }
      if (accepted) { held = route; return; }
      return json(route, initial());
    });
    await page.goto(baseURL);
    await page.getByRole("button", { name: "Install and restart desktop" }).click();
    await expect.poll(() => accepted).toBe(true);
    await page.waitForTimeout(100);
    await expect(page.getByRole("button", { name: "Check for updates", exact: true })).toBeDisabled({ timeout: 500 });
    await expect(page.getByRole("button", { name: "Cancel download" })).toBeDisabled();
    await expect.poll(() => Boolean(held), { timeout: 6000 }).toBe(true);
    await json(held, initial());
    await expect(page.getByRole("button", { name: "Check for updates", exact: true })).toBeEnabled();
  });
  await scenario("connection failure disables actions and recovers", async page => {
    let disconnected = true;
    await page.route("**/api/desktop-update/**", route => disconnected
      ? json(route, { error: "Updater disconnected" }, 503) : json(route, initial()));
    await page.goto(baseURL);
    await expect(page.getByText("Updater disconnected")).toBeVisible();
    await expect(page.getByRole("button", { name: "Install and restart desktop" })).toBeDisabled();
    disconnected = false;
    await expect(page.getByRole("button", { name: "Install and restart desktop" })).toBeEnabled({ timeout: 6000 });
    await expect(page.getByText("Updater disconnected")).toHaveCount(0);
  });
} finally {
  await browser.close();
  await server.close();
  await writeFile(resolve(output, process.env.BASELINE ? "ui-before.json" : "ui-results.json"), JSON.stringify({ physical_device: false, results }, null, 2));
}
