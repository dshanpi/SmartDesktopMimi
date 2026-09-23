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
  argument("--output", "/tmp/aitvbox-live-mcp-smoke"),
);
if (!baseUrl || !password) {
  console.error(
    "usage: ipkvm_live_mcp_smoke.mjs --url http://DEVICE_IP " +
    "--password PASSWORD [--output DIRECTORY]",
  );
  process.exit(2);
}

await mkdir(outputDirectory, { recursive: true });
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

  const before = await page.evaluate(() => ({
    peerCount: window.__aitvboxPeers.length,
    connectedPeerCount: window.__aitvboxPeers
      .filter(peer => peer.connectionState === "connected").length,
    currentTime: document.querySelector("video").currentTime,
  }));

  await page.getByRole("button", { name: "AI HDMI MCP", exact: true }).click();
  await page.getByTestId("agent-workspace").waitFor();
  await page.waitForFunction(
    () => document.querySelector("[data-testid=agent-state]")?.textContent !== "loading",
  );
  const model = await page.getByTestId("agent-model").inputValue();
  check(model.toLowerCase().includes("embedding"),
    `expected the configured embedding validation model, got ${model}`);

  await page.getByTestId("agent-task").fill(
    "Capture one HDMI frame for shared-pipeline validation. Do not send input.",
  );
  await page.getByTestId("agent-start").click();
  await page.waitForFunction(() =>
    document.querySelector("[data-testid=agent-state]")?.textContent === "failed");
  await page.waitForFunction(() =>
    document.querySelector("[data-testid=agent-history]")?.textContent
      ?.includes("embedding models cannot generate computer-control actions"));
  await page.waitForTimeout(1200);

  const after = await page.evaluate(() => ({
    peerCount: window.__aitvboxPeers.length,
    connectedPeerCount: window.__aitvboxPeers
      .filter(peer => peer.connectionState === "connected").length,
    peerStates: window.__aitvboxPeers.map(peer => peer.connectionState),
    currentTime: document.querySelector("video").currentTime,
    readyState: document.querySelector("video").readyState,
    videoWidth: document.querySelector("video").videoWidth,
    videoHeight: document.querySelector("video").videoHeight,
  }));
  check(after.peerCount === before.peerCount,
    `AI task recreated the PeerConnection: before=${before.peerCount} after=${after.peerCount}`);
  check(after.connectedPeerCount === before.connectedPeerCount &&
    after.connectedPeerCount > 0,
  `live PeerConnection was interrupted: ${JSON.stringify(after.peerStates)}`);
  check(after.currentTime > before.currentTime + 0.5,
    `video timeline did not advance during AI capture: ${before.currentTime} -> ${after.currentTime}`);
  check(after.readyState === 4 &&
    after.videoWidth === 1920 &&
    after.videoHeight === 1080,
  `video lost decode readiness during AI capture: ${JSON.stringify(after)}`);

  await page.screenshot({
    path: path.join(outputDirectory, "live-mcp-workspace.png"),
  });
  console.log(JSON.stringify({
    result: "PASS",
    model,
    before,
    after,
    screenshot: path.join(outputDirectory, "live-mcp-workspace.png"),
  }));
} finally {
  await browser.close();
}
