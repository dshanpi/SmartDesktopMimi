// mimi-update is shared by device admission, packaging and release CI.
package main

import (
	"context"
	"encoding/json"
	"flag"
	"fmt"
	"io"
	"net"
	"net/http"
	"os"
	"os/exec"
	"os/signal"
	"path/filepath"
	"strings"
	"syscall"
	"time"

	u "smartdesktopmimi/update/update"
)

func run() error {
	if len(os.Args) < 2 {
		return fmt.Errorf("usage: mimi-update {match|verify|pack|sign-index|verify-index|serve|select|install-offline|install-offline-index|status|check|download|install|cancel} [options]")
	}
	op := os.Args[1]
	if op == "inspect-ubuntu-runtime" {
		if len(os.Args) != 2 {
			return fmt.Errorf("unexpected runtime inspector arguments")
		}
		libs, e := u.InspectUbuntuRuntime()
		if e != nil {
			return e
		}
		return json.NewEncoder(os.Stdout).Encode(libs)
	}
	if op == "inspect-ubuntu" {
		f := flag.NewFlagSet(op, flag.ContinueOnError)
		baseline := f.String("baseline", "", "controlled system baseline identifier")
		if e := f.Parse(os.Args[2:]); e != nil {
			return e
		}
		if f.NArg() != 0 {
			return fmt.Errorf("unexpected arguments")
		}
		d, enrollment, e := u.InspectUbuntu(*baseline)
		if e != nil {
			return e
		}
		return json.NewEncoder(os.Stdout).Encode(map[string]any{"device": d, "enrollment": enrollment, "scope": "read-only host candidate; not installed or activation-qualified"})
	}
	if op == "platform" {
		c, e := u.LoadConfig("/etc/smartdesktopmimi/updater.json")
		if e != nil {
			return e
		}
		return u.PlatformOperation(c, os.Args[2:])
	}
	f := flag.NewFlagSet(op, flag.ContinueOnError)
	config := f.String("config", "/etc/smartdesktopmimi/updater.json", "root-owned platform configuration")
	manifest := f.String("manifest", "", "manifest JSON")
	device := f.String("device", "", "enrolled device JSON")
	enrollment := f.String("enrollment", "", "controlled hardware enrollment record")
	sig := f.String("signature", "", "detached signature")
	trust := f.String("trust", "", "platform trust JSON")
	archive := f.String("archive", "", "application archive")
	src := f.String("source", "", "curated application tree")
	out := f.String("output", "", "new output directory")
	key := f.String("key", "", "external signing key")
	factoryManifest := f.String("factory-manifest", "", "signed destination factory manifest")
	factorySignature := f.String("factory-signature", "", "destination factory signature")
	dataRoot := f.String("data-root", "/overlay/smartdesktopmimi", "storage-health diagnostic data directory")
	epoch := f.Int64("epoch", 0, "SOURCE_DATE_EPOCH")
	if e := f.Parse(os.Args[2:]); e != nil {
		return e
	}
	if f.NArg() != 0 {
		return fmt.Errorf("unexpected arguments")
	}
	switch op {
	case "check-enrollment":
		if *device == "" || *enrollment == "" {
			return fmt.Errorf("check-enrollment requires --device and --enrollment")
		}
		return u.PlatformOperation(u.Config{DeviceFile: *device, EnrollmentFile: *enrollment}, []string{"identity", *device})
	case "storage-health":
		report, err := u.StorageHealth(*dataRoot)
		if encodeErr := json.NewEncoder(os.Stdout).Encode(report); encodeErr != nil {
			return encodeErr
		}
		return err
	case "system-geometry":
		digest, size, e := u.A133Geometry()
		if e != nil {
			return e
		}
		return json.NewEncoder(os.Stdout).Encode(map[string]any{"partition_sha256": digest, "storage_bytes": size, "storage": "emmc", "boot_mode": "ab"})
	case "match":
		var m u.Manifest
		var d u.Device
		if e := u.ReadJSON(*manifest, &m); e != nil {
			return e
		}
		if e := u.ReadJSON(*device, &d); e != nil {
			return e
		}
		return u.Match(m, d)
	case "verify", "verify-manifest", "extract-factory":
		raw, e := os.ReadFile(*manifest)
		if e != nil {
			return e
		}
		signature, e := os.ReadFile(*sig)
		if e != nil {
			return e
		}
		var t u.Trust
		if e = u.ReadJSON(*trust, &t); e != nil {
			return e
		}
		m, e := u.Verify(raw, signature, t)
		if e != nil {
			return e
		}
		var d u.Device
		if e = u.ReadJSON(*device, &d); e != nil {
			return e
		}
		if e = u.Match(m, d); e != nil {
			return e
		}
		if op == "verify-manifest" {
			return nil
		}
		if op == "extract-factory" {
			return u.ExtractFactory(*archive, raw, signature, t, d, *out)
		}
		return u.CheckArchive(*archive, m, "")
	case "verify-system", "verify-system-ota":
		raw, e := os.ReadFile(*manifest)
		if e != nil {
			return e
		}
		signature, e := os.ReadFile(*sig)
		if e != nil {
			return e
		}
		var t u.Trust
		if e = u.ReadJSON(*trust, &t); e != nil {
			return e
		}
		var d u.SystemDevice
		if e = u.ReadJSON(*device, &d); e != nil {
			return e
		}
		if op == "verify-system-ota" {
			factory, e := os.ReadFile(*factoryManifest)
			if e != nil {
				return e
			}
			factorySig, e := os.ReadFile(*factorySignature)
			if e != nil {
				return e
			}
			return u.VerifySystemOTA(raw, signature, factory, factorySig, t, d, *archive)
		}
		_, e = u.VerifySystem(raw, signature, t, d, *archive)
		return e
	case "sign-index", "verify-index":
		raw, e := os.ReadFile(*manifest)
		if e != nil {
			return e
		}
		var t u.Trust
		if e = u.ReadJSON(*trust, &t); e != nil {
			return e
		}
		if op == "sign-index" {
			return u.SignIndex(raw, *key, t, *src, *sig)
		}
		signature, e := os.ReadFile(*sig)
		if e != nil {
			return e
		}
		return u.VerifyIndexDirectory(raw, signature, t, *src)
	case "pack":
		var m u.Manifest
		if e := u.ReadJSON(*manifest, &m); e != nil {
			return e
		}
		return u.Pack(*src, *out, *key, m, *epoch)
	}
	c, e := u.LoadConfig(*config)
	if e != nil {
		return e
	}
	engine := c.Engine()
	if e = engine.ValidatePaths(); e != nil {
		return e
	}
	switch op {
	case "system-check", "system-download", "system-install":
		ctx, cancel := signal.NotifyContext(context.Background(), syscall.SIGTERM, syscall.SIGINT)
		defer cancel()
		return u.SystemOperation(ctx, c, op, func(p u.SystemProgress) { _ = json.NewEncoder(os.Stdout).Encode(p) })
	case "serve":
		s, e := u.NewService(c)
		if e != nil {
			return e
		}
		ctx, cancel := signal.NotifyContext(context.Background(), syscall.SIGTERM, syscall.SIGINT)
		defer cancel()
		return s.Serve(ctx)
	case "start-selected":
		return engine.StartSelected()
	case "system-health":
		return engine.SystemHealth(nil)
	case "system-commit":
		return engine.SystemHealth(func() error {
			if e := u.CheckSystemCommit(c); e != nil {
				return e
			}
			ctx, cancel := context.WithTimeout(context.Background(), 10*time.Second)
			defer cancel()
			value, e := exec.CommandContext(ctx, "/usr/sbin/fw_printenv", "-n", "upgrade_available").Output()
			if e != nil {
				return e
			}
			if strings.TrimSpace(string(value)) != "1" {
				if strings.TrimSpace(string(value)) == "0" {
					return u.CommitSystemJournal(c)
				}
				return fmt.Errorf("unknown system upgrade state")
			}
			// Clear the boot counter before disabling rollback, checking both writes.
			if e = exec.CommandContext(ctx, "/usr/sbin/fw_setenv", "bootcount", "0").Run(); e != nil {
				return e
			}
			if e = exec.CommandContext(ctx, "/usr/sbin/fw_setenv", "upgrade_available", "0").Run(); e != nil {
				return e
			}
			return u.CommitSystemJournal(c)
		})
	case "select":
		dir, e := engine.Select()
		if e != nil {
			return e
		}
		fmt.Println(dir)
		return nil
	case "install-offline-index":
		d, err := engine.Hooks.Device()
		if err != nil {
			return err
		}
		t, err := engine.Trust()
		if err != nil {
			return err
		}
		candidate, err := u.SelectIndexDirectory(*src, c.Repository, c.Channel, d, t)
		if err != nil {
			return err
		}
		return engine.Install(filepath.Join(*src, candidate.Package.Name), candidate.Raw, candidate.Signature, c.Channel)
	case "install-offline":
		raw, e := os.ReadFile(*manifest)
		if e != nil {
			return e
		}
		signature, e := os.ReadFile(*sig)
		if e != nil {
			return e
		}
		return engine.Install(*archive, raw, signature, c.Channel)
	case "status", "check", "download", "install", "cancel":
		client := http.Client{Timeout: 5 * time.Second, Transport: &http.Transport{DialContext: func(ctx context.Context, _, _ string) (net.Conn, error) {
			return (&net.Dialer{}).DialContext(ctx, "unix", c.Socket)
		}}}
		method := "POST"
		if op == "status" {
			method = "GET"
		}
		req, e := http.NewRequest(method, "http://localhost/"+op, nil)
		if e != nil {
			return e
		}
		res, e := client.Do(req)
		if e != nil {
			return e
		}
		defer res.Body.Close()
		b, e := io.ReadAll(io.LimitReader(res.Body, 1<<20))
		if e != nil {
			return e
		}
		fmt.Print(string(b))
		if res.StatusCode >= 400 {
			return fmt.Errorf("updater returned HTTP %d", res.StatusCode)
		}
		return nil
	case "describe":
		return json.NewEncoder(os.Stdout).Encode(map[string]string{"version": u.Version, "policy": u.Policy, "state": filepath.Join(c.Root, "state.json")})
	default:
		return fmt.Errorf("unknown command %s", op)
	}
}
func main() {
	if e := run(); e != nil {
		fmt.Fprintln(os.Stderr, e)
		os.Exit(1)
	}
}
