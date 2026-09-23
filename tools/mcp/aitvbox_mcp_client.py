#!/usr/bin/env python3
"""Call the AITVBox MCP stdio server locally, through ADB, or through SSH."""

import argparse
import base64
import json
from pathlib import Path
import subprocess
import sys


PROTOCOL_VERSION = "2025-11-25"


class McpClient:
    def __init__(self, command):
        self.process = subprocess.Popen(
            command,
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            text=True,
            encoding="utf-8",
        )
        self.next_id = 1

    def send(self, method, params=None, notification=False):
        request = {"jsonrpc": "2.0", "method": method}
        if params is not None:
            request["params"] = params
        request_id = None
        if not notification:
            request_id = self.next_id
            self.next_id += 1
            request["id"] = request_id
        self.process.stdin.write(json.dumps(request, separators=(",", ":")) + "\n")
        self.process.stdin.flush()
        if notification:
            return None

        while True:
            line = self.process.stdout.readline()
            if not line:
                raise RuntimeError("MCP server closed stdout")
            try:
                response = json.loads(line)
            except ValueError as exc:
                raise RuntimeError("invalid MCP response: {!r}".format(line)) from exc
            if response.get("id") == request_id:
                if "error" in response:
                    error = response["error"]
                    raise RuntimeError(
                        "MCP error {}: {}".format(
                            error.get("code"), error.get("message")
                        )
                    )
                return response.get("result", {})

    def initialize(self):
        result = self.send("initialize", {
            "protocolVersion": PROTOCOL_VERSION,
            "capabilities": {},
            "clientInfo": {
                "name": "aitvbox-mcp-client",
                "version": "1.0.0",
            },
        })
        self.send("notifications/initialized", notification=True)
        return result

    def close(self):
        if self.process.stdin:
            self.process.stdin.close()
        terminated_by_client = False
        try:
            status = self.process.wait(timeout=1)
        except subprocess.TimeoutExpired:
            terminated_by_client = True
            self.process.terminate()
            status = self.process.wait(timeout=5)
        if status and not terminated_by_client:
            raise RuntimeError("MCP transport exited with status {}".format(status))


def transport_command(args):
    if args.transport == "local":
        return [args.program]
    if args.transport == "adb":
        command = [args.adb]
        if args.serial:
            command += ["-s", args.serial]
        return command + ["shell", args.program]
    if not args.host:
        raise SystemExit("--host is required for SSH transport")
    return [args.ssh, args.host, args.program]


def tool_call(client, name, arguments):
    result = client.send("tools/call", {"name": name, "arguments": arguments})
    if result.get("isError"):
        text = "\n".join(
            item.get("text", "") for item in result.get("content", [])
            if item.get("type") == "text"
        )
        raise RuntimeError(text or "tool returned an error")
    return result


def print_content(result):
    for item in result.get("content", []):
        if item.get("type") == "text":
            print(item.get("text", ""))
        elif item.get("type") == "image":
            print("<{} image: {} base64 characters>".format(
                item.get("mimeType", "unknown"), len(item.get("data", ""))
            ))


def execute(args, client):
    if args.command == "tools":
        result = client.send("tools/list")
        for tool in result.get("tools", []):
            print("{}\t{}".format(tool["name"], tool.get("description", "")))
        return
    if args.command == "call":
        try:
            arguments = json.loads(args.arguments)
        except ValueError as exc:
            raise RuntimeError("--arguments must be a JSON object") from exc
        if not isinstance(arguments, dict):
            raise RuntimeError("--arguments must be a JSON object")
        print_content(tool_call(client, args.name, arguments))
        return
    if args.command == "capture":
        result = tool_call(client, "computer.capture", {})
        images = [
            item for item in result.get("content", [])
            if item.get("type") == "image"
        ]
        if not images:
            raise RuntimeError("capture returned no image")
        output = Path(args.output).resolve()
        output.write_bytes(base64.b64decode(images[0]["data"], validate=True))
        print(output)
        return
    if args.command == "key":
        print_content(tool_call(client, "computer.key", {
            "keycode": args.keycode,
            "modifier": args.modifier,
        }))
        return
    if args.command == "mouse":
        print_content(tool_call(client, "computer.mouse", {
            "dx": args.dx,
            "dy": args.dy,
            "buttons": args.buttons,
            "wheel": args.wheel,
        }))
        return
    if args.command == "capabilities":
        print_content(tool_call(client, "hardware.capabilities", {}))
        return
    if args.command == "stop":
        print_content(tool_call(client, "safety.stop", {}))


def main():
    parser = argparse.ArgumentParser(prog="aitvbox-mcp-client")
    parser.add_argument(
        "--transport", choices=("adb", "ssh", "local"), default="adb"
    )
    parser.add_argument("--program", default="/usr/bin/aitvbox-mcpd")
    parser.add_argument("--adb", default="adb")
    parser.add_argument("--serial")
    parser.add_argument("--ssh", default="ssh")
    parser.add_argument("--host", help="SSH destination, for example root@192.0.2.10")
    sub = parser.add_subparsers(dest="command", required=True)
    sub.add_parser("tools")
    call = sub.add_parser("call")
    call.add_argument("name")
    call.add_argument("--arguments", default="{}")
    capture = sub.add_parser("capture")
    capture.add_argument("-o", "--output", default="aitvbox-screen.jpg")
    key = sub.add_parser("key")
    key.add_argument("keycode", type=int)
    key.add_argument("--modifier", type=int, default=0)
    mouse = sub.add_parser("mouse")
    mouse.add_argument("dx", type=int)
    mouse.add_argument("dy", type=int)
    mouse.add_argument("--buttons", type=int, default=0)
    mouse.add_argument("--wheel", type=int, default=0)
    sub.add_parser("capabilities")
    sub.add_parser("stop")
    args = parser.parse_args()

    client = McpClient(transport_command(args))
    try:
        client.initialize()
        execute(args, client)
        client.close()
    except (OSError, RuntimeError, ValueError) as exc:
        print("aitvbox-mcp-client: {}".format(exc), file=sys.stderr)
        if client.process.poll() is None:
            client.process.terminate()
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
