import { defineConfig } from "vite";
import react from "@vitejs/plugin-react";
import { execFile } from "node:child_process";
import { resolve } from "node:path";
import type { Plugin } from "vite";

const host = process.env.TAURI_DEV_HOST;

const validSerial = (serial: unknown): serial is string =>
  typeof serial === "string" &&
  serial.length > 0 &&
  /^[A-Za-z0-9_:-]+$/.test(serial);

const automaticTestIds = new Set([
  "identity",
  "storage",
  "memory",
  "wifi",
  "bluetooth",
  "environment",
  "power",
  "audio",
  "touch-controller"
]);

function runFactoryCli(args: string[]): Promise<unknown> {
  const manifest = resolve(process.cwd(), "factory-core", "Cargo.toml");
  return new Promise((resolveResult, reject) => {
    execFile(
      "cargo",
      ["run", "--offline", "--quiet", "--manifest-path", manifest, "--bin", "factory_cli", "--", ...args],
      { timeout: 30_000, maxBuffer: 1024 * 1024 },
      (error, stdout, stderr) => {
        if (error) {
          reject(new Error(stderr.trim() || error.message));
          return;
        }
        try {
          resolveResult(JSON.parse(stdout));
        } catch {
          reject(new Error("本地 ADB 桥返回了无效数据"));
        }
      }
    );
  });
}

function readJsonBody(request: NodeJS.ReadableStream): Promise<Record<string, unknown>> {
  return new Promise((resolveBody, reject) => {
    let body = "";
    request.on("data", chunk => {
      body += String(chunk);
      if (body.length > 8192) reject(new Error("请求内容过大"));
    });
    request.on("end", () => {
      try {
        resolveBody(body ? JSON.parse(body) : {});
      } catch {
        reject(new Error("请求格式无效"));
      }
    });
    request.on("error", reject);
  });
}

function localAdbBridge(): Plugin {
  return {
    name: "aitvbox-local-adb-bridge",
    configureServer(server) {
      server.middlewares.use(async (request, response, next) => {
        const path = request.url?.split("?", 1)[0];
        if (!path?.startsWith("/api/factory/")) {
          next();
          return;
        }
        response.setHeader("Content-Type", "application/json; charset=utf-8");
        try {
          let result: unknown;
          if (path === "/api/factory/scan" && request.method === "POST") {
            result = await runFactoryCli(["scan"]);
          } else if (
            (path === "/api/factory/hardware" || path === "/api/factory/hardware-test" || path === "/api/factory/tone" || path === "/api/factory/microphone") &&
            request.method === "POST"
          ) {
            const body = await readJsonBody(request);
            if (!validSerial(body.serial)) throw new Error("ADB 序列号格式无效");
            if (path.endsWith("/hardware-test")) {
              if (typeof body.id !== "string" || !automaticTestIds.has(body.id)) {
                throw new Error("自动检测项目无效");
              }
              result = await runFactoryCli(["hardware-test", body.serial, body.id]);
            } else {
              const operation = path.endsWith("/tone")
                ? "tone"
                : path.endsWith("/microphone")
                  ? "microphone"
                  : "hardware";
              result = await runFactoryCli([operation, body.serial]);
            }
          } else {
            response.statusCode = 404;
            response.end(JSON.stringify({ error: "接口不存在" }));
            return;
          }
          response.statusCode = 200;
          response.end(JSON.stringify(result));
        } catch (error) {
          response.statusCode = 500;
          response.end(JSON.stringify({ error: error instanceof Error ? error.message : String(error) }));
        }
      });
    }
  };
}

export default defineConfig({
  plugins: [react(), localAdbBridge()],
  clearScreen: false,
  server: {
    port: 1420,
    strictPort: true,
    host: host || false,
    hmr: host ? { protocol: "ws", host, port: 1421 } : undefined,
    watch: { ignored: ["**/src-tauri/**"] }
  }
});
