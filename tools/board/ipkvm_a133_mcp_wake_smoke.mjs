#!/usr/bin/env node

import process from "node:process";
import { createRequire } from "node:module";

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
const holdMilliseconds = Number(argument("--hold-ms", "10000"));
if (!baseUrl || !password || !Number.isFinite(holdMilliseconds) ||
    holdMilliseconds < 1000) {
  console.error(
    "usage: ipkvm_a133_mcp_wake_smoke.mjs --url http://DEVICE_IP " +
    "--password PASSWORD [--hold-ms MILLISECONDS]",
  );
  process.exit(2);
}

const browser = await chromium.launch({
  headless: true,
  args: ["--enable-unsafe-swiftshader"],
});

try {
  const page = await browser.newPage({ viewport: { width: 1440, height: 900 } });
  page.setDefaultTimeout(70000);
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
  await page.getByPlaceholder("Enter your password").fill(password);
  await page.getByRole("button", { name: "Log In" }).click();
  await page.waitForURL(url => !url.pathname.includes("login"));
  await page.waitForFunction(() => {
    const video = document.querySelector("video");
    return window.__aitvboxPeers.some(peer => peer.connectionState === "connected") &&
      video?.readyState === 4 &&
      video.videoWidth === 1920 &&
      video.videoHeight === 1080;
  });

  await page.getByRole("button", { name: "AI HDMI MCP", exact: true }).click();
  await page.getByTestId("agent-workspace").waitFor();
  await page.waitForFunction(() =>
    document.querySelector("[data-testid=agent-state]")?.textContent !== "loading");
  await page.getByTestId("agent-task").fill(
    "A133 HDMI MCP auto-wake validation. Do not send keyboard or mouse input.",
  );
  const controls = await page.evaluate(() => ({
    state: document.querySelector("[data-testid=agent-state]")?.textContent,
    endpoint: document.querySelector("[data-testid=agent-endpoint]")?.value,
    model: document.querySelector("[data-testid=agent-model]")?.value,
    startDisabled: document.querySelector("[data-testid=agent-start]")?.disabled,
    workspaceText: document.querySelector("[data-testid=agent-workspace]")
      ?.textContent?.replace(/\s+/g, " ").slice(0, 500),
  }));
  console.log(JSON.stringify({ event: "controls-ready", controls }));
  check(
    controls.workspaceText?.includes("configured"),
    `agent UI did not report configured credentials: ${JSON.stringify(controls)}`,
  );
  const beforeTime = await page.locator("video").evaluate(video => video.currentTime);
  await page.getByTestId("agent-start").click();
  await page.waitForFunction(() =>
    document.querySelector("[data-testid=agent-state]")?.textContent === "running");

  console.log(JSON.stringify({
    event: "running",
    peerCount: await page.evaluate(() => window.__aitvboxPeers.length),
    videoTime: beforeTime,
  }));
  await page.waitForTimeout(holdMilliseconds);

  const during = await page.evaluate(() => ({
    connectedPeerCount: window.__aitvboxPeers
      .filter(peer => peer.connectionState === "connected").length,
    currentTime: document.querySelector("video")?.currentTime,
    readyState: document.querySelector("video")?.readyState,
  }));
  check(during.connectedPeerCount > 0, "live WebRTC disconnected during MCP task");
  check(during.currentTime > beforeTime + 0.5,
    `video timeline stalled during MCP task: ${beforeTime} -> ${during.currentTime}`);
  check(during.readyState === 4,
    `video lost decode readiness during MCP task: ${during.readyState}`);

  const stateBeforeStop = await page.getByTestId("agent-state").textContent();
  const emergencyStop = page.getByTestId("agent-stop");
  if (await emergencyStop.isEnabled()) {
    await emergencyStop.click();
  } else {
    const releaseControl = page.getByTestId("agent-resume-video");
    if (await releaseControl.count()) await releaseControl.click();
  }
  await page.waitForFunction(() =>
    document.querySelector("[data-testid=agent-state]")?.textContent !== "running");
  console.log(JSON.stringify({
    result: "PASS",
    beforeTime,
    during,
    stateBeforeStop,
  }));
} finally {
  await browser.close();
}
