#!/usr/bin/env node

import process from "node:process";
import { createRequire } from "node:module";
import { mkdir } from "node:fs/promises";
import path from "node:path";

const require = createRequire(import.meta.url);
const { chromium } = require(
  "../../apps/ipkvm/upstream/ui/node_modules/@playwright/test",
);

function argument(name, fallback = "") {
  const index = process.argv.indexOf(name);
  return index >= 0 && index + 1 < process.argv.length
    ? process.argv[index + 1]
    : fallback;
}

function check(condition, message) {
  if (!condition) throw new Error(message);
}

const baseUrl = argument("--url").replace(/\/$/, "");
const password = argument("--password");
const outputDirectory = path.resolve(
  argument("--output", "/tmp/aitvbox-agent-browser-smoke"),
);
if (!baseUrl || !password) {
  console.error(
    "usage: ipkvm_agent_browser_smoke.mjs --url http://DEVICE_IP " +
    "--password PASSWORD [--output DIRECTORY]",
  );
  process.exit(2);
}

const testKey = "aitvbox-browser-smoke-placeholder";
const testEndpoint = "https://10.255.255.1/v1/chat/completions";
const defaultEndpoint = "https://api.openai.com/v1/chat/completions";
const defaultModel = "gpt-4o-mini";

await mkdir(outputDirectory, { recursive: true });
const browser = await chromium.launch({
  headless: true,
  args: ["--enable-unsafe-swiftshader"],
});

try {
  const page = await browser.newPage({ viewport: { width: 1440, height: 900 } });
  page.setDefaultTimeout(70000);
  const errors = [];
  let loggedIn = false;
  page.on("pageerror", error => errors.push(`page: ${error.message}`));
  page.on("console", message => {
    if (message.type() !== "error") return;
    const text = message.text();
    if (text.startsWith("Failed to load resource:")) return;
    if (!loggedIn && text.includes("401 (Unauthorized)")) return;
    errors.push(`console: ${text}`);
  });
  await page.route(/\.(woff2?|ttf)(\?.*)?$/, route => route.abort());
  await page.addInitScript(() => {
    window.__aitvboxPeers = [];
    const OriginalPeerConnection = window.RTCPeerConnection;
    window.RTCPeerConnection = class extends OriginalPeerConnection {
      constructor(...parameters) {
        super(...parameters);
        window.__aitvboxPeers.push(this);
      }
    };
  });

  await page.goto(`${baseUrl}/mode`, { waitUntil: "domcontentloaded" });
  const passwordInput = page.getByPlaceholder("Enter your password");
  await passwordInput.waitFor();
  await passwordInput.fill(password);
  await page.getByRole("button", { name: "Log In" }).click();
  await page.waitForURL(url => !url.pathname.includes("login"));
  loggedIn = true;

  await page.waitForFunction(
    () => window.__aitvboxPeers.some(peer => peer.connectionState === "connected"),
  );
  await page.getByText("Settings", { exact: true }).click();
  await page.getByRole("button", { name: "HDMI + HID MCP" }).click();
  await page.getByTestId("agent-state").waitFor();

  const serviceSection = page.locator("section").filter({ hasText: "Service status" });
  const endpointInput = page.getByTestId("agent-endpoint");
  const modelInput = page.getByTestId("agent-model");
  await endpointInput.fill(testEndpoint);
  await modelInput.fill("aitvbox-browser-smoke");
  await page.getByRole("button", { name: "Save provider" }).click();
  await checkServiceText(page, serviceSection, "Model provider: configured");

  const keyInput = page.getByTestId("agent-api-key");
  await keyInput.fill(testKey);
  await page.getByRole("button", { name: "Save API key" }).click();
  await checkServiceText(page, serviceSection, "API key: configured");
  await page.waitForFunction(() =>
    document.querySelector("[data-testid=agent-api-key]")?.value === "");
  check(await keyInput.inputValue() === "", "API key input was not cleared");
  check(!(await page.locator("body").innerText()).includes(testKey),
    "API key leaked into rendered page text");

  await page.getByTestId("agent-task").fill(
    "Read the current HDMI frame. Do not send keyboard or mouse input.",
  );
  await page.getByTestId("agent-start").click();
  await page.waitForFunction(() => {
    const state = document.querySelector("[data-testid=agent-state]")?.textContent;
    return state === "running" || state === "failed";
  });
  const startedState = (await page.getByTestId("agent-state").innerText()).trim();
  check(startedState === "running",
    `agent did not enter running state: ${startedState}`);
  await page.screenshot({
    path: path.join(outputDirectory, "agent-running.png"),
    fullPage: true,
  });

  await page.getByTestId("agent-stop").click();
  await page.waitForFunction(() => {
    const state = document.querySelector("[data-testid=agent-state]")?.textContent;
    return state && state !== "running" && state !== "stopping";
  });
  const stoppedState = (await page.getByTestId("agent-state").innerText()).trim();
  check(["idle", "failed", "succeeded"].includes(stoppedState),
    `agent did not stop cleanly: ${stoppedState}`);

  await page.getByTestId("agent-resume-video").click();
  await page.waitForFunction(
    () => window.__aitvboxPeers.some(peer => peer.connectionState === "connected"),
  );
  await page.waitForFunction(() => {
    const video = document.querySelector("video");
    return video?.readyState === 4 && video.currentTime > 0;
  });

  // Leave no test credential or private test endpoint on the device.
  await endpointInput.fill(defaultEndpoint);
  await modelInput.fill(defaultModel);
  await page.getByRole("button", { name: "Save provider" }).click();
  await checkServiceText(page, serviceSection, "Model provider: configured");
  await page.getByRole("button", { name: "Clear API key" }).click();
  await checkServiceText(page, serviceSection, "API key: missing");

  const dimensions = await page.evaluate(() => ({
    viewportWidth: document.documentElement.clientWidth,
    pageWidth: document.documentElement.scrollWidth,
    viewportHeight: document.documentElement.clientHeight,
    pageHeight: document.documentElement.scrollHeight,
  }));
  check(dimensions.pageWidth <= dimensions.viewportWidth,
    `agent settings overflow horizontally: ${JSON.stringify(dimensions)}`);
  if (errors.length) throw new Error(`browser errors:\n${errors.join("\n")}`);

  console.log(JSON.stringify({
    result: "PASS",
    startedState,
    stoppedState,
    restoredEndpoint: await endpointInput.inputValue(),
    restoredModel: await modelInput.inputValue(),
    dimensions,
    screenshot: path.join(outputDirectory, "agent-running.png"),
  }));
} finally {
  await browser.close();
}

async function checkServiceText(page, section, expected) {
  await section.waitFor();
  await page.waitForFunction(
    ({ expectedText }) => [...document.querySelectorAll("section")]
      .some(element => element.textContent?.includes("Service status") &&
        element.textContent?.replace(/\s+/g, " ").includes(expectedText)),
    { expectedText: expected },
  );
}
