package update

import (
	"fmt"
	"regexp"
	"strings"
)

var systemTagPattern = regexp.MustCompile(`^system-([a-z0-9]+(?:-[a-z0-9]+)*)-v(.+)$`)

func releaseIdentity(tag string) (kind, platform, version string) {
	if strings.HasPrefix(tag, "v") {
		if _, err := parseVersion(tag[1:]); err == nil {
			return "application", "", tag[1:]
		}
	}
	parts := systemTagPattern.FindStringSubmatch(tag)
	if parts != nil {
		if _, err := parseVersion(parts[2]); err == nil {
			return "system", parts[1], parts[2]
		}
	}
	return "", "", ""
}

func systemReleaseTag(m SystemManifest) (string, error) {
	if len(m.Targets) == 0 {
		return "", fmt.Errorf("system release has no target")
	}
	platform := m.Targets[0].Identity["platform_id"]
	tag := "system-" + platform + "-v" + m.Version
	k, p, _ := releaseIdentity(tag)
	if k != "system" || p != platform {
		return "", fmt.Errorf("invalid system tag identity")
	}
	for _, target := range m.Targets {
		if target.Identity["platform_id"] != platform {
			return "", fmt.Errorf("system release mixes platforms")
		}
	}
	return tag, nil
}
