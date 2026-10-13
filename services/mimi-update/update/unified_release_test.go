package update

import (
	"context"
	"testing"
)

func appendRelease(releases *[]Release, assets map[string][]byte, tag string, draft, pre bool, files map[string][]byte) {
	r := Release{Tag: tag, Draft: draft, Prerelease: pre}
	for name, b := range files {
		url := "https://github.com/test/host-only/releases/download/" + tag + "/" + name
		r.Assets = append(r.Assets, Asset{name, url, int64(len(b))})
		assets[url] = b
	}
	*releases = append(*releases, r)
}

func TestUnifiedRepositorySelectsSeparateReleaseTypes(t *testing.T) {
	m, d, trust := fixture(t)
	_, raw, sig, payload := makePackage(t, m)
	sys, sd, _, swu := systemFixture(t)
	appFiles := map[string][]byte{"manifest.json": raw, "manifest.sig": sig, "application.tar.gz": payload}
	sysFiles := map[string][]byte{"system-manifest.json": sys.Raw, "system-manifest.sig": sys.Signature, "factory-manifest.json": sys.FactoryRaw, "factory-manifest.sig": sys.FactorySignature, "system.swu": swu}
	assets := map[string][]byte{}
	var releases []Release
	appendRelease(&releases, assets, "v1.1.0", false, false, appFiles)
	appendRelease(&releases, assets, "system-a133-v1.1.0", false, false, sysFiles)
	appendRelease(&releases, assets, "v9.0.0", true, false, appFiles)
	appendRelease(&releases, assets, "system-a133-v9.0.0", true, false, sysFiles)
	appendRelease(&releases, assets, "v8.0.0-rc.1", false, true, appFiles)
	appendRelease(&releases, assets, "system-a133-v8.0.0-rc.1", false, true, sysFiles)
	// A malformed index on a different release line must never enter app parsing.
	appendRelease(&releases, assets, "system-rk3576-v1.1.0", false, false, map[string][]byte{"release-index.json": []byte("invalid")})
	g := testGitHub(t, releases, assets)
	a, err := g.Check(context.Background(), d, trust, "stable", "1.0.0", 0)
	if err != nil || a == nil || a.Manifest.Type != "application" || a.Manifest.Version != "1.1.0" {
		t.Fatalf("application selection: %v %v", a, err)
	}
	s, err := g.CheckSystem(context.Background(), sd, trust, "stable", 0)
	if err != nil || s == nil {
		t.Fatalf("system selection: %v %v", s, err)
	}
	verified, err := s.Verify(sd, trust, "stable", 0)
	if err != nil || verified.Type != "system" || verified.Version != "1.1.0" {
		t.Fatalf("system verification: %v %v", verified, err)
	}
}

func TestUnifiedSystemRejectsWrongTagTypePlatformSignatureAndAssets(t *testing.T) {
	for _, mode := range []string{"application-tag", "wrong-platform", "version", "signature", "missing-asset", "wrong-type"} {
		t.Run(mode, func(t *testing.T) {
			c, d, trust, payload := systemFixture(t)
			tag := "system-a133-v1.1.0"
			switch mode {
			case "application-tag":
				tag = "v1.1.0"
			case "wrong-platform":
				tag = "system-t527-v1.1.0"
			case "version":
				tag = "system-a133-v1.2.0"
			case "signature":
				c.Signature[0] ^= 1
			case "wrong-type":
				c.Raw = c.FactoryRaw
				c.Signature = c.FactorySignature
			}
			files := map[string][]byte{"system-manifest.json": c.Raw, "system-manifest.sig": c.Signature, "factory-manifest.json": c.FactoryRaw, "factory-manifest.sig": c.FactorySignature, "system.swu": payload}
			if mode == "missing-asset" {
				delete(files, "factory-manifest.sig")
			}
			assets := map[string][]byte{}
			var releases []Release
			appendRelease(&releases, assets, tag, false, false, files)
			selected, err := testGitHub(t, releases, assets).CheckSystem(context.Background(), d, trust, "stable", 0)
			if err != nil || selected != nil {
				t.Fatalf("invalid candidate accepted: %v %v", selected, err)
			}
		})
	}
}

func TestUnifiedApplicationCannotSelectSystemPayload(t *testing.T) {
	c, d, trust, payload := systemFixture(t)
	for _, tag := range []string{"v1.1.0", "system-a133-v1.1.0"} {
		assets := map[string][]byte{}
		var releases []Release
		appendRelease(&releases, assets, tag, false, false, map[string][]byte{"manifest.json": c.Raw, "manifest.sig": c.Signature, "application.tar.gz": payload})
		candidate, err := testGitHub(t, releases, assets).Check(context.Background(), d.Device, trust, "stable", "1.0.0", 0)
		if err != nil || candidate != nil {
			t.Fatalf("system selected as application: %v %v", candidate, err)
		}
	}
}

func TestSystemReleaseTagRejectsMixedPlatforms(t *testing.T) {
	c, _, _, _ := systemFixture(t)
	var m SystemManifest
	if err := StrictJSON(c.Raw, &m); err != nil {
		t.Fatal(err)
	}
	if tag, err := systemReleaseTag(m); err != nil || tag != "system-a133-v1.1.0" {
		t.Fatalf("tag: %s %v", tag, err)
	}
	extra := clone(m.Targets[0])
	extra.Identity["platform_id"] = "rk3576"
	m.Targets = append(m.Targets, extra)
	if _, err := systemReleaseTag(m); err == nil {
		t.Fatal("mixed system platforms accepted")
	}
}

func TestUnifiedRepositoryConfiguration(t *testing.T) {
	c := Config{Repository: "dshanpi/SmartDesktopMimi", SystemRepository: "dshanpi/SmartDesktopMimi", SystemDeviceFile: "/etc/smartdesktopmimi/system-device.json"}
	if err := c.validateSystemConfig(); err != nil {
		t.Fatal(err)
	}
	c.SystemDeviceFile = "../device.json"
	if err := c.validateSystemConfig(); err == nil {
		t.Fatal("invalid system descriptor path accepted")
	}
	c.SystemDeviceFile = "/etc/system.json"
	c.SystemRepository = "bad url"
	if err := c.validateSystemConfig(); err == nil {
		t.Fatal("invalid system repository accepted")
	}
}
