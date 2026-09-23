#!/usr/bin/env node

import process from "node:process";
import { createRequire } from "node:module";
import { mkdir, readFile } from "node:fs/promises";
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

const baseUrl = argument("--url");
let password = argument("--password");
const passwordFile = argument("--password-file");
const authConfig = argument("--auth-config");
const videoOnly = process.argv.includes("--video-only");
const softwareRenderer = process.argv.includes("--software-renderer");
const settleMs = Number(argument("--settle-ms", "10000"));
const outputDirectory = path.resolve(argument("--output", "/tmp/aitvbox-browser-smoke"));
if (passwordFile) {
  password = (await readFile(passwordFile, "utf8")).trim();
}
if (!baseUrl || (!password && !authConfig)) {
  console.error(
    "usage: ipkvm_browser_smoke.mjs --url http://DEVICE_IP " +
    "(--password PASSWORD | --password-file FILE | --auth-config CONFIG_JSON) " +
    "[--output DIRECTORY] " +
    "[--video-only] [--software-renderer] [--settle-ms MILLISECONDS]",
  );
  process.exit(2);
}
check(Number.isFinite(settleMs) && settleMs >= 0, "settle time must be non-negative");

await mkdir(outputDirectory, { recursive: true });
const browser = await chromium.launch({
  headless: true,
  args: softwareRenderer
    ? ["--disable-gpu"]
    : ["--enable-unsafe-swiftshader"],
});

try {
 testRun: {
  const context = await browser.newContext({ viewport: { width: 1440, height: 900 } });
  if (authConfig) {
    const config = JSON.parse(await readFile(authConfig, "utf8"));
    check(
      typeof config.local_auth_token === "string" && config.local_auth_token.length > 0,
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
    window.__aitvboxRpcMethods = [];
    window.__aitvboxRpcErrors = [];
    window.__aitvboxHidSends = [];
    const OriginalPeerConnection = window.RTCPeerConnection;
    window.RTCPeerConnection = class extends OriginalPeerConnection {
      constructor(...parameters) {
        super(...parameters);
        window.__aitvboxPeers.push(this);
      }

      createDataChannel(...parameters) {
        const channel = super.createDataChannel(...parameters);
        const originalSend = channel.send.bind(channel);
        const requests = new Map();
        if (channel.label === "rpc") {
          channel.addEventListener("message", event => {
            try {
              const response = JSON.parse(event.data);
              if (response.error && requests.has(response.id)) {
                window.__aitvboxRpcErrors.push({
                  method: requests.get(response.id),
                  error: response.error,
                });
              }
            } catch {
              // Non-JSON data is handled by the application itself.
            }
          });
        }
        channel.send = data => {
          if (channel.label.startsWith("hidrpc")) {
            window.__aitvboxHidSends.push({
              label: channel.label,
              bytes: typeof data === "string" ? data.length : data.byteLength,
            });
          }
          if (channel.label === "rpc" && typeof data === "string") {
            try {
              const request = JSON.parse(data);
              window.__aitvboxRpcMethods.push(request.method);
              requests.set(request.id, request.method);
            } catch {
              // The smoke test records valid JSON-RPC method names only.
            }
          }
          return originalSend(data);
        };
        return channel;
      }
    };
  });

  if (authConfig) {
    await page.goto(`${baseUrl.replace(/\/$/, "")}/`, {
      waitUntil: "domcontentloaded",
      timeout: 70000,
    });
    check(
      !new URL(page.url()).pathname.includes("login"),
      "stored device session token was rejected",
    );
  } else {
    await page.goto(`${baseUrl.replace(/\/$/, "")}/mode`, {
      waitUntil: "domcontentloaded",
      timeout: 70000,
    });
    const passwordInput = page.getByPlaceholder("Enter your password");
    await passwordInput.waitFor();
    check(
      new URL(page.url()).pathname === "/login-local",
      `/mode did not redirect to password login: ${page.url()}`,
    );
    await passwordInput.fill(password);
    await page.getByRole("button", { name: "Log In" }).click();
    await page.waitForURL(url => !url.pathname.includes("login"), {
      timeout: 30000,
    });
  }
  loggedIn = true;

  try {
    await page.waitForFunction(
      () => window.__aitvboxPeers.some(peer => peer.connectionState === "connected"),
      null,
      { timeout: 30000 },
    );
  } catch (error) {
    const diagnostics = await page.evaluate(async () =>
      Promise.all(window.__aitvboxPeers.map(async peer => {
        const stats = [...(await peer.getStats()).values()]
          .filter(entry => [
            "candidate-pair", "local-candidate", "remote-candidate",
          ].includes(entry.type));
        return {
          connectionState: peer.connectionState,
          iceConnectionState: peer.iceConnectionState,
          iceGatheringState: peer.iceGatheringState,
          signalingState: peer.signalingState,
          localDescription: peer.localDescription?.sdp,
          remoteDescription: peer.remoteDescription?.sdp,
          stats,
        };
      })),
    );
    await page.screenshot({
      path: path.join(outputDirectory, "ipkvm-connect-failure.png"),
    });
    throw new Error(
      `${error.message}; peer diagnostics=${JSON.stringify(diagnostics)}; ` +
      `browser errors=${JSON.stringify(errors)}`,
    );
  }
  try {
    await page.waitForFunction(() => {
      const video = document.querySelector("video");
      return video?.readyState === 4 &&
        video.videoWidth === 1920 &&
        video.videoHeight === 1080 &&
        video.currentTime > 0;
    }, null, { timeout: 30000 });
  } catch (error) {
    const diagnostics = await page.evaluate(async () => ({
      video: [...document.querySelectorAll("video")].map(video => ({
        readyState: video.readyState,
        networkState: video.networkState,
        paused: video.paused,
        currentTime: video.currentTime,
        width: video.videoWidth,
        height: video.videoHeight,
        error: video.error?.message,
        tracks: video.srcObject instanceof MediaStream
          ? video.srcObject.getTracks().map(track => ({
            kind: track.kind,
            muted: track.muted,
            enabled: track.enabled,
            readyState: track.readyState,
          }))
          : [],
      })),
      peers: await Promise.all(window.__aitvboxPeers.map(async peer => ({
        connectionState: peer.connectionState,
        iceConnectionState: peer.iceConnectionState,
        stats: [...(await peer.getStats()).values()].filter(entry =>
          ["inbound-rtp", "codec", "candidate-pair"].includes(entry.type)),
      }))),
    }));
    await page.screenshot({
      path: path.join(outputDirectory, "ipkvm-video-failure.png"),
    });
    throw new Error(
      `${error.message}; video diagnostics=${JSON.stringify(diagnostics)}; ` +
      `browser errors=${JSON.stringify(errors)}`,
    );
  }
  await page.waitForTimeout(3000);

  check(
    await page.getByText("Connection Issue Detected").count() === 0,
    "connected video is covered by the disconnected overlay",
  );
  const video = await page.locator("video").first().evaluate(element => ({
    width: element.videoWidth,
    height: element.videoHeight,
    readyState: element.readyState,
    currentTime: element.currentTime,
  }));
  await page.getByTestId("hid-channel-state").filter({ hasText: "READY" }).waitFor();

  const frameSamples = [];
  for (let sample = 0; sample < 24; sample++) {
    frameSamples.push(await page.getByTestId("video-frame").evaluate(element => {
      const rect = element.getBoundingClientRect();
      return {
        x: Math.round(rect.x * 10) / 10,
        y: Math.round(rect.y * 10) / 10,
        width: Math.round(rect.width * 10) / 10,
        height: Math.round(rect.height * 10) / 10,
      };
    }));
    await page.waitForTimeout(100);
  }
  const frameSpread = Object.fromEntries(["x", "y", "width", "height"].map(key => [
    key,
    Math.max(...frameSamples.map(sample => sample[key])) -
      Math.min(...frameSamples.map(sample => sample[key])),
  ]));
  check(
    Object.values(frameSpread).every(spread => spread <= 1),
    `video frame is still jumping: ${JSON.stringify(frameSpread)}`,
  );

  if (videoOnly) {
    await page.waitForTimeout(settleMs);
    const browserH264Capabilities = await page.evaluate(() =>
      (RTCRtpReceiver.getCapabilities("video")?.codecs ?? [])
        .filter(codec => codec.mimeType.toLowerCase() === "video/h264")
        .map(codec => ({
          clockRate: codec.clockRate,
          sdpFmtpLine: codec.sdpFmtpLine,
        })),
    );
    const inboundVideo = await page.evaluate(async () => {
      const peers = window.__aitvboxPeers
        .filter(peer => peer.connectionState === "connected");
      const stats = [];
      for (const peer of peers) {
        const report = await peer.getStats();
        for (const entry of report.values()) {
          if (entry.type === "inbound-rtp" && entry.kind === "video") {
            const codec = report.get(entry.codecId);
            stats.push({
              packetsReceived: entry.packetsReceived,
              packetsLost: entry.packetsLost,
              bytesReceived: entry.bytesReceived,
              framesReceived: entry.framesReceived,
              framesDecoded: entry.framesDecoded,
              framesDropped: entry.framesDropped,
              keyFramesDecoded: entry.keyFramesDecoded,
              freezeCount: entry.freezeCount,
              totalFreezesDuration: entry.totalFreezesDuration,
              totalDecodeTime: entry.totalDecodeTime,
              jitter: entry.jitter,
              jitterBufferDelay: entry.jitterBufferDelay,
              decoderImplementation: entry.decoderImplementation,
              powerEfficientDecoder: entry.powerEfficientDecoder,
              codec: codec ? {
                payloadType: codec.payloadType,
                mimeType: codec.mimeType,
                clockRate: codec.clockRate,
                sdpFmtpLine: codec.sdpFmtpLine,
              } : null,
            });
          }
        }
      }
      return stats;
    });
    check(inboundVideo.length > 0, "browser exposed no inbound video statistics");
    check(
      inboundVideo.every(stat => (stat.framesDecoded ?? 0) > 0),
      `browser decoded no video frames: ${JSON.stringify(inboundVideo)}`,
    );
    const screenshot = path.join(outputDirectory, "ipkvm-video-only.png");
    await page.screenshot({ path: screenshot });
    if (errors.length) {
      throw new Error(`browser errors:\n${errors.join("\n")}`);
    }
    console.log(JSON.stringify({
      result: "PASS",
      mode: "video-only",
      video,
      frameSpread,
      browserH264Capabilities,
      inboundVideo,
      screenshot,
    }));
    break testRun;
  }

  // A real control session commonly starts after a browser refresh. Pointer
  // Lock never survives navigation, so verify that the freshly loaded UI can
  // explicitly reacquire it before accepting keyboard or relative mouse input.
  await page.reload({ waitUntil: "domcontentloaded" });
  await page.waitForFunction(
    () => window.__aitvboxPeers.some(peer => peer.connectionState === "connected"),
  );
  await page.waitForFunction(() => {
    const video = document.querySelector("video");
    return video?.readyState === 4 && video.currentTime > 0;
  });
  await page.getByTestId("hid-channel-state").filter({ hasText: "READY" }).waitFor();

  const hidBefore = await page.evaluate(() => window.__aitvboxHidSends.length);
  const videoElement = page.getByTestId("kvm-video");
  await page.getByTestId("enable-kvm-input").click();
  await page.waitForFunction(
    () => document.pointerLockElement === document.querySelector("[data-testid=kvm-video]"),
  );
  await page.keyboard.down("Shift");
  await page.keyboard.up("Shift");
  const box = await videoElement.boundingBox();
  check(box && box.width > 0 && box.height > 0, "video has no input area");
  const mouseBefore = await page.evaluate(() => window.__aitvboxHidSends.length);
  await page.mouse.move(box.x + box.width / 2, box.y + box.height / 2);
  await page.mouse.move(box.x + box.width / 2 + 30, box.y + box.height / 2 + 20);
  await page.waitForFunction(
    previous => window.__aitvboxHidSends.length >= previous + 3,
    hidBefore,
  );
  const hidInput = await page.evaluate(previous => {
    const sends = window.__aitvboxHidSends.slice(previous);
    return {
      count: sends.length,
      channels: [...new Set(sends.map(send => send.label))],
    };
  }, hidBefore);
  check(
    hidInput.channels.includes("hidrpc"),
    `keyboard did not use the HID RPC channel: ${JSON.stringify(hidInput)}`,
  );
  const mouseInput = await page.evaluate(previous => {
    const sends = window.__aitvboxHidSends.slice(previous);
    return {
      count: sends.length,
      channels: [...new Set(sends.map(send => send.label))],
    };
  }, mouseBefore);
  check(
    mouseInput.count > 0 && mouseInput.channels.every(label => label === "hidrpc"),
    `mouse did not use the reliable HID channel: ${JSON.stringify(mouseInput)}`,
  );
  await page.screenshot({
    path: path.join(outputDirectory, "ipkvm-control-console.png"),
  });
  await page.evaluate(() => {
    if (document.pointerLockElement) document.exitPointerLock();
  });
  await page.waitForFunction(() => document.pointerLockElement === null);

  await page.getByRole("button", { name: "AI HDMI MCP", exact: true }).click();
  await page.getByTestId("agent-workspace").waitFor();
  await page.waitForFunction(
    () => document.querySelector("[data-testid=agent-state]")?.textContent !== "loading",
  );
  const agentState = await page.getByTestId("agent-state").innerText();
  const endpoint = await page.getByTestId("agent-endpoint").inputValue();
  const model = await page.getByTestId("agent-model").inputValue();
  await page.waitForTimeout(2000);
  const dimensions = await page.evaluate(() => ({
    viewportWidth: document.documentElement.clientWidth,
    pageWidth: document.documentElement.scrollWidth,
    viewportHeight: document.documentElement.clientHeight,
    pageHeight: document.documentElement.scrollHeight,
  }));
  check(
    dimensions.pageWidth <= dimensions.viewportWidth,
    `AI workspace caused horizontal overflow: ${JSON.stringify(dimensions)}`,
  );
  check(endpoint.startsWith("https://"), "agent endpoint is not HTTPS");
  check(model.length > 0, "agent model is empty");
  await page.getByTestId("agent-history").waitFor();

  await page.screenshot({
    path: path.join(outputDirectory, "ipkvm-agent-workspace.png"),
  });
  await page.getByRole("button", { name: "Applications", exact: true }).click();
  await page.getByTestId("applications-workspace").waitFor();
  await page.getByText("2048 example: build, trust, upload, run").waitFor();
  await page.getByTestId("app-package").waitFor();
  const appDimensions = await page.evaluate(() => ({
    viewportWidth: document.documentElement.clientWidth,
    pageWidth: document.documentElement.scrollWidth,
    viewportHeight: document.documentElement.clientHeight,
    pageHeight: document.documentElement.scrollHeight,
  }));
  check(
    appDimensions.pageWidth <= appDimensions.viewportWidth,
    `application manager overflowed the browser: ${JSON.stringify(appDimensions)}`,
  );
  await page.screenshot({
    path: path.join(outputDirectory, "ipkvm-applications.png"),
  });
  await page.getByRole("button", { name: "Live", exact: true }).click();

  const { rpcMethods, rpcErrors } = await page.evaluate(() => ({
    rpcMethods: window.__aitvboxRpcMethods,
    rpcErrors: window.__aitvboxRpcErrors,
  }));
  if (errors.length) {
    throw new Error(
      `browser errors:\n${errors.join("\n")}\n` +
      `RPC errors: ${JSON.stringify(rpcErrors)}`,
    );
  }
  console.log(JSON.stringify({
    result: "PASS",
    video,
    frameSpread,
    hidInput,
    mouseInput,
    agentState,
    endpoint,
    model,
    dimensions,
    appDimensions,
    rpcMethods: [...new Set(rpcMethods)],
    screenshot: path.join(outputDirectory, "ipkvm-agent-workspace.png"),
  }));
 }
} finally {
  await browser.close();
}
