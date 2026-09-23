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
  argument("--output") || "/tmp/aitvbox-keyboard-smoke",
);
if (passwordFile) {
  password = (await readFile(passwordFile, "utf8")).trim();
}
if (!baseUrl || (!password && !authConfig)) {
  console.error(
    "usage: ipkvm_keyboard_smoke.mjs --url http://DEVICE_IP " +
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
    window.__aitvboxKeyboardSends = [];
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
          if (channel.label.startsWith("hidrpc") && bytes?.[0] === 2) {
            window.__aitvboxKeyboardSends.push({
              label: channel.label,
              type: bytes[0],
              bytes: [...bytes],
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

  await page.getByTestId("enable-kvm-input").click();
  await page.waitForFunction(
    () => document.pointerLockElement ===
      document.querySelector("[data-testid=kvm-video]"),
  );
  await page.evaluate(() => {
    window.__aitvboxKeyboardSends.length = 0;
  });
  const videoFingerprint = async () => page.evaluate(() => {
    const video = document.querySelector("[data-testid=kvm-video]");
    const canvas = document.createElement("canvas");
    canvas.width = 160;
    canvas.height = 90;
    const context = canvas.getContext("2d", { willReadFrequently: true });
    context.drawImage(video, 0, 0, canvas.width, canvas.height);
    return [...context.getImageData(0, 0, canvas.width, canvas.height).data];
  });
  const meanFrameDifference = (first, second) => {
    let total = 0;
    for (let index = 0; index < first.length; index += 4) {
      total += Math.abs(first[index] - second[index]);
      total += Math.abs(first[index + 1] - second[index + 1]);
      total += Math.abs(first[index + 2] - second[index + 2]);
    }
    return total / (first.length / 4 * 3);
  };
  const beforeFrame = await videoFingerprint();
  await page.screenshot({
    path: path.join(outputDirectory, "before-win-d.png"),
  });

  const toggleDesktop = async () => {
    await page.keyboard.down("Meta");
    await page.keyboard.press("KeyD");
    await page.keyboard.up("Meta");
    await page.waitForTimeout(1500);
  };

  // Win+D is handled by the Windows host itself, so this verifies the whole
  // browser -> WebRTC -> hidg -> USB host keyboard path without depending on
  // whether VMware currently owns guest input.
  await toggleDesktop();
  const firstFrame = await videoFingerprint();
  await page.screenshot({
    path: path.join(outputDirectory, "after-first-win-d.png"),
  });
  await toggleDesktop();
  const secondFrame = await videoFingerprint();
  await page.screenshot({
    path: path.join(outputDirectory, "after-second-win-d.png"),
  });

  const sends = await page.evaluate(() => window.__aitvboxKeyboardSends);
  check(sends.length >= 8, `too few keyboard reports: ${JSON.stringify(sends)}`);
  check(
    sends.every(send => send.label === "hidrpc" && send.type === 2),
    `keyboard used an unexpected channel or message type: ${JSON.stringify(sends)}`,
  );
  check(
    sends.at(-1)?.bytes?.length === 2 && sends.at(-1).bytes[1] === 0,
    `keyboard modifier remained pressed: ${JSON.stringify(sends)}`,
  );
  const firstDifference = meanFrameDifference(beforeFrame, firstFrame);
  const secondDifference = meanFrameDifference(firstFrame, secondFrame);
  check(
    firstDifference > 10 && secondDifference > 10,
    `Win+D did not visibly toggle the host: ${JSON.stringify({
      firstDifference,
      secondDifference,
    })}`,
  );
  await page.evaluate(() => {
    if (document.pointerLockElement) document.exitPointerLock();
  });
  await page.waitForFunction(() => document.pointerLockElement === null);

  console.log(JSON.stringify({
    result: "PASS",
    sends,
    frameDifference: {
      first: firstDifference,
      second: secondDifference,
    },
    before: path.join(outputDirectory, "before-win-d.png"),
    afterFirst: path.join(outputDirectory, "after-first-win-d.png"),
    afterSecond: path.join(outputDirectory, "after-second-win-d.png"),
  }));
} finally {
  await browser.close();
}
