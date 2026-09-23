#!/usr/bin/env python3
"""Initialize, build, verify and upload signed AITVBox applications."""

import argparse
import getpass
import hashlib
import http.cookiejar
import json
import os
from pathlib import Path
import re
import secrets
import shutil
import ssl
import struct
import subprocess
import tarfile
import tempfile
import urllib.error
import urllib.parse
import urllib.request

APP_ID = re.compile(r"^[a-z][a-z0-9]*(\.[a-z0-9][a-z0-9-]*)+$")
VERSION = re.compile(r"^[0-9]+\.[0-9]+\.[0-9]+$")
PERMISSIONS = {
    "capture.snapshot", "input.keyboard", "input.pointer",
    "hardware.gpio.read", "hardware.gpio.write", "hardware.i2c",
    "hardware.spi", "hardware.uart", "hardware.usb",
    "network.https", "storage.app",
}
ALLOWED_SUFFIXES = {".json", ".png", ".jpg", ".jpeg", ".ttf", ".otf"}
PAYLOAD_PATH = re.compile(r"^[A-Za-z0-9._/-]+$")
MAX_PACKAGE_BYTES = 32 * 1024 * 1024
MAX_FILE_BYTES = 8 * 1024 * 1024
MAX_EXTRACTED_BYTES = 64 * 1024 * 1024


def fail(message):
    raise SystemExit("aitapp: " + message)


def json_no_duplicates(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            fail("duplicate JSON field: " + key)
        result[key] = value
    return result


def read_json(path, description):
    try:
        return json.loads(
            path.read_text(encoding="utf-8"),
            object_pairs_hook=json_no_duplicates,
        )
    except (OSError, UnicodeError, ValueError) as exc:
        fail("invalid {}: {}".format(description, exc))


def utf8_length(value):
    return len(value.encode("utf-8"))


def validate_ui(root, manifest):
    entry = root / manifest["ui"]["entry"]
    page = read_json(entry, "UI entry")
    if not isinstance(page, dict) or set(page) != {"schema", "title", "layout"}:
        fail("UI entry fields do not match schema version 1")
    if page["schema"] != 1:
        fail("unsupported UI schema")
    if not isinstance(page["title"], str) or \
            not 1 <= utf8_length(page["title"]) <= 64:
        fail("UI title length must be 1..64")

    node_count = [0]

    def validate_node(node, depth):
        node_count[0] += 1
        if depth > 8 or node_count[0] > 128 or not isinstance(node, dict):
            fail("UI layout exceeds nesting or node limits")
        node_type = node.get("type")
        if node_type == "column":
            if set(node) != {"type", "children"} or not isinstance(node["children"], list):
                fail("invalid column node")
            if len(node["children"]) > 64:
                fail("column has too many children")
            for child in node["children"]:
                validate_node(child, depth + 1)
        elif node_type == "text":
            if set(node) != {"type", "text"} or not isinstance(node["text"], str):
                fail("invalid text node")
            if utf8_length(node["text"]) > 4096:
                fail("text node exceeds 4096 characters")
        elif node_type == "action":
            if not {"type", "label", "command"} <= set(node) or \
                    set(node) - {"type", "label", "command", "arguments"}:
                fail("invalid action node")
            if not isinstance(node["label"], str) or \
                    not 1 <= utf8_length(node["label"]) <= 64:
                fail("action label length must be 1..64")
            command = node["command"]
            if not isinstance(command, str):
                fail("unsupported action command")
            if command == "app.invoke":
                if manifest["schema"] != 2:
                    fail("app.invoke requires a native application")
                arguments = node.get("arguments")
                if not isinstance(arguments, dict) or set(arguments) != {"name"} or \
                        not isinstance(arguments["name"], str) or \
                        not re.fullmatch(r"[A-Za-z][A-Za-z0-9._-]{0,63}",
                                         arguments["name"]):
                    fail("app.invoke requires a valid action name")
            else:
                if command != "capture.snapshot":
                    fail("only capture.snapshot is available as a direct UI action")
                if command not in manifest["permissions"]:
                    fail("action command requires manifest permission: " + command)
                if "arguments" in node and (
                        not isinstance(node["arguments"], dict) or node["arguments"]):
                    fail("action arguments must currently be an empty object")
        else:
            fail("unsupported UI node type")

    validate_node(page["layout"], 1)


def load_manifest(root):
    data = read_json(root / "manifest.json", "manifest.json")
    required = {"schema", "id", "name", "version", "publisher", "ui", "permissions"}
    if not isinstance(data, dict) or data.get("schema") not in {1, 2}:
        fail("manifest schema must be 1 or 2")
    if data["schema"] == 2:
        required.add("runtime")
    if set(data) != required:
        fail("manifest fields do not match schema version")
    if not isinstance(data["id"], str) or \
            not APP_ID.fullmatch(data["id"]) or len(data["id"]) > 96:
        fail("invalid application id")
    if not isinstance(data["version"], str) or \
            not VERSION.fullmatch(data["version"]) or \
            len(data["version"]) > 32 or \
            any(int(part) > 0xFFFFFFFF for part in data["version"].split(".")):
        fail("version must be MAJOR.MINOR.PATCH")
    if not isinstance(data["name"], str) or \
            not 1 <= utf8_length(data["name"]) <= 48:
        fail("name length must be 1..48")
    if not isinstance(data["publisher"], str) or \
            not re.fullmatch(r"[A-Za-z0-9._-]{1,64}", data["publisher"]):
        fail("invalid publisher")
    ui = data["ui"]
    if not isinstance(ui, dict) or \
            set(ui) != {"entry", "presentation"} or \
            ui["presentation"] != "embedded":
        fail("only embedded presentation is accepted")
    if not isinstance(ui["entry"], str) or \
            not re.fullmatch(r"ui/[A-Za-z0-9._/-]+\.json", ui["entry"]) or \
            len(ui["entry"].encode("ascii")) > 192 or \
            any(part in {"", ".", ".."} for part in ui["entry"].split("/")):
        fail("invalid UI entry")
    if not isinstance(data["permissions"], list):
        fail("permissions must be an array")
    if not all(isinstance(permission, str) for permission in data["permissions"]):
        fail("permissions must contain strings")
    if len(data["permissions"]) != len(set(data["permissions"])):
        fail("permissions must be unique")
    unknown = set(data["permissions"]) - PERMISSIONS
    if unknown:
        fail("unsupported permissions: " + ", ".join(sorted(unknown)))
    if data["schema"] == 2:
        runtime = data["runtime"]
        if not isinstance(runtime, dict) or \
                set(runtime) != {"type", "entry", "api"} or \
                runtime["type"] != "native-rpc" or runtime["api"] != 1:
            fail("unsupported native runtime")
        if not isinstance(runtime["entry"], str) or \
                not re.fullmatch(r"bin/[A-Za-z0-9][A-Za-z0-9._-]{0,63}",
                                 runtime["entry"]):
            fail("invalid native runtime entry")
    entry = (root / ui["entry"]).resolve()
    if root.resolve() not in entry.parents or not entry.is_file():
        fail("UI entry is missing or escapes app directory")
    validate_ui(root, data)
    return data


def validate_native_elf(path):
    try:
        header = path.read_bytes()[:64]
    except OSError as exc:
        fail("cannot read native runtime: {}".format(exc))
    if len(header) < 64 or header[:4] != b"\x7fELF" or \
            header[4:7] != b"\x02\x01\x01":
        fail("native runtime must be a 64-bit little-endian ELF")
    elf_type, machine, version = struct.unpack_from("<HHI", header, 16)
    entry = struct.unpack_from("<Q", header, 24)[0]
    if elf_type not in {2, 3} or machine != 183 or version != 1 or entry == 0:
        fail("native runtime must be an executable AArch64 ELF")


def payload_files(root, manifest):
    result = []
    total_size = 0
    runtime_entry = Path(manifest["runtime"]["entry"]) \
        if manifest["schema"] == 2 else None
    runtime_found = False
    for path in sorted(root.rglob("*")):
        if path.is_symlink():
            fail("executables and symlinks are prohibited: {}".format(
                path.relative_to(root)
            ))
        if not path.is_file():
            continue
        relative = path.relative_to(root)
        if manifest["schema"] == 2 and relative.parts[0] == "src":
            continue
        if not PAYLOAD_PATH.fullmatch(relative.as_posix()):
            fail("invalid payload path: {}".format(relative))
        if relative.parts[0] == "data":
            fail("data is a reserved runtime directory")
        is_runtime = relative == runtime_entry
        try:
            with path.open("rb") as stream:
                prefix = stream.read(4)
        except OSError as exc:
            fail("cannot read payload: {}".format(exc))
        if is_runtime:
            if not path.stat().st_mode & 0o111:
                fail("native runtime entry must be executable")
            validate_native_elf(path)
            runtime_found = True
        elif path.stat().st_mode & 0o111:
            fail("executables and symlinks are prohibited: {}".format(relative))
        elif prefix.startswith(b"#!") or prefix == b"\x7fELF":
            fail("scripts, libraries and extra ELF payloads are prohibited: {}".format(
                relative
            ))
        if relative.name in {"SIGNATURE", "SHA256SUMS"}:
            continue
        if not is_runtime and path.suffix.lower() not in ALLOWED_SUFFIXES:
            fail("unsupported payload type: {}".format(relative))
        if path.stat().st_size > MAX_FILE_BYTES:
            fail("file exceeds 8 MiB: {}".format(relative))
        total_size += path.stat().st_size
        if total_size > MAX_EXTRACTED_BYTES:
            fail("application payload exceeds 64 MiB")
        result.append(relative)
    if not result:
        fail("empty application")
    if len(result) > 254:
        fail("application has too many files (maximum 254)")
    if runtime_entry is not None and not runtime_found:
        fail("native runtime entry is missing")
    return result


def checksums(root, files):
    lines = []
    for relative in files:
        digest = hashlib.sha256((root / relative).read_bytes()).hexdigest()
        lines.append("{}  {}".format(digest, relative.as_posix()))
    return ("\n".join(lines) + "\n").encode("ascii")


def normalize_tar_member(member):
    member.uid = 0
    member.gid = 0
    member.uname = "root"
    member.gname = "root"
    return member


def run_openssl(*args):
    try:
        subprocess.run(["openssl"] + list(args), check=True)
    except (OSError, subprocess.CalledProcessError):
        fail("OpenSSL operation failed")


def init_app(args):
    target = Path(args.directory).resolve()
    if target.exists() and (not target.is_dir() or any(target.iterdir())):
        fail("target directory is not empty")
    if not APP_ID.fullmatch(args.app_id) or len(args.app_id) > 96:
        fail("invalid application id")
    if not isinstance(args.name, str) or not 1 <= len(args.name) <= 48:
        fail("name length must be 1..48")
    if not re.fullmatch(r"[A-Za-z0-9._-]{1,64}", args.publisher):
        fail("invalid publisher")

    manifest = {
        "schema": 1,
        "id": args.app_id,
        "name": args.name,
        "version": "1.0.0",
        "publisher": args.publisher,
        "ui": {
            "entry": "ui/main.json",
            "presentation": "embedded",
        },
        "permissions": [],
    }
    if args.native:
        manifest["schema"] = 2
        manifest["runtime"] = {
            "type": "native-rpc",
            "entry": "bin/app",
            "api": 1,
        }
    page = {
        "schema": 1,
        "title": args.name,
        "layout": {
            "type": "column",
            "children": [
                {
                    "type": "text",
                    "text": "Edit ui/main.json to build your application.",
                },
            ],
        },
    }
    if args.native:
        page["layout"]["children"].append({
            "type": "action",
            "label": "Run application action",
            "command": "app.invoke",
            "arguments": {"name": "main"},
        })
    (target / "ui").mkdir(parents=True, exist_ok=True)
    (target / "manifest.json").write_text(
        json.dumps(manifest, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )
    (target / "ui/main.json").write_text(
        json.dumps(page, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )
    if args.native:
        (target / "src").mkdir()
        (target / "src/main.c").write_text(
            '#include "aitvbox/app.h"\n\n'
            "int main(int argc, char **argv)\n"
            "{\n"
            "    const char *action = aitvbox_app_action(argc, argv);\n"
            "    if (!action)\n"
            "        return 2;\n"
            "    return aitvbox_app_reply(true, action);\n"
            "}\n",
            encoding="ascii",
        )
    print(target)


def build(args):
    source = Path(args.source).resolve()
    manifest = load_manifest(source)
    files = payload_files(source, manifest)
    output = Path(args.output or "{}-{}.aitapp".format(
        manifest["id"], manifest["version"])).resolve()
    if not output.parent.is_dir():
        fail("output directory does not exist")
    temporary_output = None
    try:
        with tempfile.TemporaryDirectory(prefix="aitapp-") as tmp_name:
            tmp = Path(tmp_name)
            for relative in files:
                target = tmp / relative
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(str(source / relative), str(target))
            (tmp / "SHA256SUMS").write_bytes(checksums(tmp, files))
            run_openssl("dgst", "-sha256", "-sign", args.key,
                        "-out", str(tmp / "SIGNATURE"),
                        str(tmp / "SHA256SUMS"))
            descriptor, temporary_name = tempfile.mkstemp(
                prefix=".{}.tmp.".format(output.name), dir=str(output.parent)
            )
            os.close(descriptor)
            temporary_output = Path(temporary_name)
            with tarfile.open(
                    str(temporary_output), "w:gz",
                    format=tarfile.PAX_FORMAT) as archive:
                for path in sorted(tmp.rglob("*")):
                    if path.is_file():
                        archive.add(
                            str(path),
                            arcname=path.relative_to(tmp).as_posix(),
                            filter=normalize_tar_member,
                        )
            if temporary_output.stat().st_size > MAX_PACKAGE_BYTES:
                fail("built package exceeds 32 MiB")
            os.replace(str(temporary_output), str(output))
            temporary_output = None
    finally:
        if temporary_output is not None:
            temporary_output.unlink(missing_ok=True)
    print(output)


def compile_native(args):
    source = Path(args.source).resolve()
    manifest = load_manifest(source)
    if manifest["schema"] != 2:
        fail("compile requires a schema 2 native-rpc application")
    sdk = Path(args.sdk).expanduser().resolve()
    if not sdk.is_dir():
        fail("Tina SDK directory does not exist")
    compiler = Path(args.cc).expanduser().resolve() if args.cc else None
    config = sdk / "out/a133/b6/openwrt/tmp/.config"
    if compiler is None and config.is_file():
        values = {}
        for line in config.read_text(encoding="utf-8").splitlines():
            match = re.fullmatch(
                r'CONFIG_TOOLCHAIN_(ROOT|PREFIX)="(.*)"', line
            )
            if match:
                values[match.group(1)] = match.group(2)
        if {"ROOT", "PREFIX"} <= set(values):
            root = values["ROOT"].replace("$(LICHEE_TOP_DIR)", str(sdk))
            compiler = Path(root) / "bin" / (values["PREFIX"] + "gcc")
    if compiler is None:
        compiler_candidates = sorted(sdk.glob(
            "prebuilt/rootfsbuilt/aarch64/*/toolchain/bin/"
            "aarch64-openwrt-linux-gcc"
        ))
        compiler = compiler_candidates[0] if compiler_candidates else None
    if not compiler or not compiler.is_file():
        fail("cannot locate the A133 aarch64-openwrt-linux-gnu-gcc compiler")
    sysroot = sdk / "out/a133/b6/openwrt/staging_dir/target"
    if not (sysroot / "usr/include/json-c/json.h").is_file():
        fail("SDK target sysroot is incomplete; build the SDK first")
    sources = sorted((source / "src").glob("*.c"))
    if not sources:
        fail("native application has no src/*.c files")
    runtime = source / manifest["runtime"]["entry"]
    runtime.parent.mkdir(parents=True, exist_ok=True)
    app_sdk = Path(__file__).resolve().parents[2] / "platform/app-sdk"
    command = [
        str(compiler), "-std=gnu11", "-O2", "-fPIE", "-pie",
        "-Wall", "-Wextra", "-Werror",
        "-I", str(app_sdk / "include"),
        "-I", str(sysroot / "usr/include"),
        *[str(path) for path in sources],
        str(app_sdk / "src/app.c"),
        "-o", str(runtime),
        "-L", str(sysroot / "usr/lib"),
        "-Wl,-rpath-link,{}".format(sysroot / "lib"),
        "-Wl,-rpath-link,{}".format(sysroot / "usr/lib"),
        "-Wl,-rpath-link,{}".format(sysroot / "root-a133-b6/lib"),
        "-Wl,-rpath-link,{}".format(sysroot / "root-a133-b6/usr/lib"),
        "-ljson-c",
    ]
    environment = os.environ.copy()
    environment["STAGING_DIR"] = str(
        sdk / "out/a133/b6/openwrt/staging_dir"
    )
    try:
        subprocess.run(command, check=True, env=environment)
    except (OSError, subprocess.CalledProcessError):
        runtime.unlink(missing_ok=True)
        fail("native A133 compilation failed")
    runtime.chmod(0o755)
    validate_native_elf(runtime)
    print(runtime)


def safe_extract(package, destination):
    try:
        if package.stat().st_size > MAX_PACKAGE_BYTES:
            fail("package exceeds 32 MiB")
    except OSError as exc:
        fail("cannot read package: {}".format(exc))
    try:
        with tarfile.open(str(package), "r:gz") as archive:
            members = archive.getmembers()
            if len(members) > 256:
                fail("package has too many archive members")
            names = set()
            total_size = 0
            for member in members:
                path = Path(member.name)
                if member.name in names:
                    fail("duplicate archive member: {}".format(member.name))
                names.add(member.name)
                if len(member.name) > 240 or not PAYLOAD_PATH.fullmatch(member.name):
                    fail("invalid archive member name: {}".format(member.name))
                if member.issym() or member.islnk() or path.is_absolute() or \
                        any(part in {"", ".", ".."} for part in path.parts):
                    fail("unsafe archive member: {}".format(member.name))
                if not member.isfile() and not member.isdir():
                    fail("unsupported archive member: {}".format(member.name))
                if member.isfile() and member.size > MAX_FILE_BYTES:
                    fail("archive member exceeds 8 MiB: {}".format(member.name))
                if member.isfile():
                    total_size += member.size
                    if total_size > MAX_EXTRACTED_BYTES:
                        fail("archive payload exceeds 64 MiB")
            archive.extractall(str(destination))
    except (OSError, tarfile.TarError) as exc:
        fail("cannot read package: {}".format(exc))


def verify(args):
    package = Path(args.package).resolve()
    with tempfile.TemporaryDirectory(prefix="aitapp-verify-") as tmp_name:
        root = Path(tmp_name)
        safe_extract(package, root)
        manifest = load_manifest(root)
        files = payload_files(root, manifest)
        expected = checksums(root, files)
        try:
            actual = (root / "SHA256SUMS").read_bytes()
        except OSError:
            fail("SHA256SUMS is missing")
        if expected != actual:
            fail("payload checksum list does not match")
        if not (root / "SIGNATURE").is_file():
            fail("SIGNATURE is missing")
        run_openssl("dgst", "-sha256", "-verify", args.public_key,
                    "-signature", str(root / "SIGNATURE"), str(root / "SHA256SUMS"))
    print("OK {}".format(package))


def upload(args):
    verify(argparse.Namespace(package=args.package, public_key=args.public_key))
    package = Path(args.package).resolve()
    parsed = urllib.parse.urlsplit(args.url)
    if parsed.scheme not in {"http", "https"} or not parsed.netloc or \
            parsed.query or parsed.fragment:
        fail("device URL must be an http:// or https:// origin")
    base_url = args.url.rstrip("/")

    cookie_jar = http.cookiejar.CookieJar()
    handlers = [urllib.request.HTTPCookieProcessor(cookie_jar)]
    if args.insecure:
        if parsed.scheme != "https":
            fail("--insecure is only valid with an https:// device URL")
        handlers.append(urllib.request.HTTPSHandler(
            context=ssl._create_unverified_context(),
        ))
    opener = urllib.request.build_opener(*handlers)

    def open_request(request, timeout):
        try:
            return opener.open(request, timeout=timeout)
        except urllib.error.HTTPError as exc:
            try:
                payload = json.loads(exc.read(65536).decode("utf-8"))
                detail = payload.get("error")
            except (UnicodeError, ValueError, AttributeError):
                detail = None
            fail(detail or "device returned HTTP {}".format(exc.code))
        except urllib.error.URLError as exc:
            fail("cannot connect to device: {}".format(exc.reason))

    status_request = urllib.request.Request(
        base_url + "/device/status",
        headers={"Accept": "application/json"},
    )
    with open_request(status_request, 15) as response:
        try:
            status = json.loads(response.read(65536).decode("utf-8"))
        except (UnicodeError, ValueError):
            fail("device returned an invalid status response")
    if not status.get("isSetup"):
        fail("device setup must be completed in the browser first")

    if status.get("passwordRequired"):
        password = args.password or os.environ.get("AITVBOX_PASSWORD")
        if args.password_file:
            try:
                password = Path(args.password_file).read_text(
                    encoding="utf-8"
                ).rstrip("\r\n")
            except (OSError, UnicodeError) as exc:
                fail("cannot read password file: {}".format(exc))
        if not password and os.isatty(0):
            password = getpass.getpass("AITVBox password: ")
        if not password:
            fail("set AITVBOX_PASSWORD or use --password-file")
        login_body = json.dumps({"password": password}).encode("utf-8")
        login_request = urllib.request.Request(
            base_url + "/auth/login-local",
            data=login_body,
            method="POST",
            headers={
                "Accept": "application/json",
                "Content-Type": "application/json",
            },
        )
        with open_request(login_request, 15):
            pass

    boundary = "----aitapp-{}".format(secrets.token_hex(16))
    package_bytes = package.read_bytes()
    filename = package.name.replace('"', "_").replace("\r", "_").replace("\n", "_")
    multipart_body = (
        "--{}\r\n".format(boundary).encode("ascii") +
        ('Content-Disposition: form-data; name="package"; filename="{}"\r\n'
         .format(filename)).encode("utf-8") +
        b"Content-Type: application/octet-stream\r\n\r\n" +
        package_bytes +
        "\r\n--{}--\r\n".format(boundary).encode("ascii")
    )
    mode = "replace" if args.replace else (
        "downgrade" if args.allow_downgrade else "upgrade"
    )
    install_url = base_url + "/api/apps/install?" + urllib.parse.urlencode(
        {"mode": mode}
    )
    install_request = urllib.request.Request(
        install_url,
        data=multipart_body,
        method="POST",
        headers={
            "Accept": "application/json",
            "Content-Type": "multipart/form-data; boundary={}".format(boundary),
        },
    )
    with open_request(install_request, 180) as response:
        try:
            result = json.loads(response.read(65536).decode("utf-8"))
        except (UnicodeError, ValueError):
            fail("device returned an invalid install response")
    try:
        installed = result["app"]
        print("OK {} {}".format(installed["id"], installed["version"]))
    except (KeyError, TypeError):
        fail("device returned an invalid install result")


def main():
    parser = argparse.ArgumentParser(prog="aitapp")
    sub = parser.add_subparsers(dest="command", required=True)
    init_parser = sub.add_parser("init")
    init_parser.add_argument("directory")
    init_parser.add_argument("--id", dest="app_id", required=True)
    init_parser.add_argument("--name", required=True)
    init_parser.add_argument("--publisher", required=True)
    init_parser.add_argument(
        "--native", action="store_true",
        help="create a schema 2 native-rpc application skeleton",
    )
    init_parser.set_defaults(func=init_app)
    build_parser = sub.add_parser("build")
    build_parser.add_argument("source")
    build_parser.add_argument("--key", required=True)
    build_parser.add_argument("-o", "--output")
    build_parser.set_defaults(func=build)
    compile_parser = sub.add_parser("compile")
    compile_parser.add_argument("source")
    compile_parser.add_argument("--sdk", required=True)
    compile_parser.add_argument("--cc")
    compile_parser.set_defaults(func=compile_native)
    verify_parser = sub.add_parser("verify")
    verify_parser.add_argument("package")
    verify_parser.add_argument("--public-key", required=True)
    verify_parser.set_defaults(func=verify)
    upload_parser = sub.add_parser("upload")
    upload_parser.add_argument("package")
    upload_parser.add_argument("--public-key", required=True)
    upload_parser.add_argument(
        "--url", required=True,
        help="AITVBox browser origin, for example http://192.168.1.50",
    )
    password_source = upload_parser.add_mutually_exclusive_group()
    password_source.add_argument(
        "--password",
        help="device password (AITVBOX_PASSWORD or --password-file is safer)",
    )
    password_source.add_argument("--password-file")
    upload_parser.add_argument(
        "--insecure", action="store_true",
        help="accept a self-signed HTTPS certificate",
    )
    upload_mode = upload_parser.add_mutually_exclusive_group()
    upload_mode.add_argument("--replace", action="store_true")
    upload_mode.add_argument("--allow-downgrade", action="store_true")
    upload_parser.set_defaults(func=upload)
    args = parser.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()
