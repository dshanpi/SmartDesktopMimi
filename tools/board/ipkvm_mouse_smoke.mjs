#!/usr/bin/env node

import process from "node:process";
import { createRequire } from "node:module";
import { mkdir, readFile } from "node:fs/promises";
import path from "node:path";

const require = createRequire(import.meta.url);
const { chromium } = require(
  "../../apps/ipkvm/upstream/ui/node_modules/@playwright/test",
);

function argument(name) {
  const index = process.argv.indexOf(name);
  return index >= 0 && index + 1 < process.argv.length
    ? process.argv[index + 1]
    : "";
}

function check(condition, message) {
  if (!condition) throw new Error(message);
}

const baseUrl = argument("--url").replace(/\/$/, "");
let password = argument("--password");
const passwordFile = argument("--password-file");
const authConfig = argument("--auth-config");
const outputDirectory = path.resolve(
  argument("--output") || "/tmp/aitvbox-mouse-smoke",
);
if (passwordFile) {
  password = (await readFile(passwordFile, "utf8")).trim();
}
if (!baseUrl || (!password && !authConfig)) {
  console.error(
    "usage: ipkvm_mouse_smoke.mjs --url http://DEVICE_IP " +
    "(--password PASSWORD | --password-file FILE | --auth-config CONFIG_JSON) " +
    "[--output DIRECTORY]",
  );
  process.exit(2);
}

await mkdir(outputDirectory, { recursive: true });
const browser = await chromium.launch({
  headless: true,
  args: ["--enable-unsafe-swiftshader"],
});

try {
  const context = await browser.newContext({ viewport: { width: 1440, height: 900 } });
  if (authConfig) {
    const config = JSON.parse(await readFile(authConfig, "utf8"));
    check(
      typeof config.local_auth_token === "string" &&
        config.local_auth_token.length > 0,
      "auth config does not contain a session token",
    );
    await context.addCookies([{
      name: "authToken",
      value: config.local_auth_token,
      url: baseUrl,
      httpOnly: true,
      sameSite: "Strict",
    }]);
  }
  const page = await context.newPage();
  page.setDefaultTimeout(70000);
  await page.route(/\.(woff2?|ttf)(\?.*)?$/, route => route.abort());
  await page.addInitScript(() => {
    window.__aitvboxMouseSends = [];
    const OriginalPeerConnection = window.RTCPeerConnection;
    window.RTCPeerConnection = class extends OriginalPeerConnection {
      createDataChannel(...parameters) {
        const channel = super.createDataChannel(...parameters);
        const originalSend = channel.send.bind(channel);
        channel.send = data => {
          const bytes = data instanceof ArrayBuffer
            ? new Uint8Array(data)
            : ArrayBuffer.isView(data)
              ? new Uint8Array(data.buffer, data.byteOffset, data.byteLength)
              : null;
          if (channel.label.startsWith("hidrpc") && bytes?.byteLength) {
            window.__aitvboxMouseSends.push({
              label: channel.label,
              type: bytes[0],
              bytes: bytes.byteLength,
            });
          }
          return originalSend(data);
        };
        return channel;
      }
    };
  });

  await page.goto(`${baseUrl}/mode`, { waitUntil: "domcontentloaded" });
  if (!authConfig) {
    await page.getByPlaceholder("Enter your password").fill(password);
    await page.getByRole("button", { name: "Log In" }).click();
    await page.waitForURL(url => !url.pathname.includes("login"));
  }
  await page.waitForFunction(() => {
    const video = document.querySelector("[data-testid=kvm-video]");
    return video?.readyState === 4 &&
      video.videoWidth === 1920 &&
      video.videoHeight === 1080 &&
      video.currentTime > 2 &&
      document.querySelector("[data-testid=hid-channel-state]")
        ?.textContent?.includes("READY");
  });

  const video = page.getByTestId("kvm-video");
  const box = await video.boundingBox();
  check(box && box.width > 240 && box.height > 120, "video input area is unavailable");
  await page.getByTestId("enable-kvm-input").click();
  await page.waitForFunction(
    () => document.pointerLockElement ===
      document.querySelector("[data-testid=kvm-video]"),
  );
  await page.evaluate(() => {
    window.__aitvboxMouseSends.length = 0;
  });
  await page.screenshot({
    path: path.join(outputDirectory, "before-mouse-move.png"),
  });

  await page.mouse.move(box.x + box.width * 0.35, box.y + box.height * 0.45);
  await page.mouse.move(box.x + box.width * 0.65, box.y + box.height * 0.45, {
    steps: 6,
  });
  await page.waitForFunction(() => window.__aitvboxMouseSends.length >= 2);
  await page.waitForTimeout(1000);
  await page.screenshot({
    path: path.join(outputDirectory, "after-mouse-move.png"),
  });

  const sends = await page.evaluate(() => window.__aitvboxMouseSends);
  check(
    sends.every(send => send.label === "hidrpc"),
    `mouse used a non-reliable channel: ${JSON.stringify(sends)}`,
  );
  await page.evaluate(() => {
    if (document.pointerLockElement) document.exitPointerLock();
  });
  await page.waitForFunction(() => document.pointerLockElement === null);
  console.log(JSON.stringify({
    result: "PASS",
    sends,
    before: path.join(outputDirectory, "before-mouse-move.png"),
    after: path.join(outputDirectory, "after-mouse-move.png"),
  }));
} finally {
  await browser.close();
}
