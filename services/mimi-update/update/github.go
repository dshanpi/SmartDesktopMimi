package update

import (
	"context"
	"encoding/json"
	"fmt"
	"io"
	"net/http"
	"net/url"
	"os"
	"path/filepath"
	"regexp"
	"strings"
	"syscall"
	"time"
)

type Asset struct {
	Name string `json:"name"`
	URL  string `json:"browser_download_url"`
	Size int64  `json:"size"`
}
type Release struct {
	Tag        string  `json:"tag_name"`
	Draft      bool    `json:"draft"`
	Prerelease bool    `json:"prerelease"`
	Assets     []Asset `json:"assets"`
}
type Candidate struct {
	Manifest  Manifest `json:"manifest"`
	Raw       []byte   `json:"-"`
	Signature []byte   `json:"-"`
	Package   Asset    `json:"-"`
}
type GitHub struct {
	Repository string
	Client     *http.Client
	Progress   func(int64, int64)
}

var repoPattern = regexp.MustCompile(`^[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+$`)

func safeGitHubURL(s string) bool {
	u, e := url.Parse(s)
	if e != nil || u.Scheme != "https" || u.User != nil || u.Port() != "" {
		return false
	}
	switch u.Host {
	case "api.github.com", "github.com", "release-assets.githubusercontent.com", "objects.githubusercontent.com":
		return true
	}
	return false
}
func NewGitHub(repo string) (*GitHub, error) {
	if !repoPattern.MatchString(repo) {
		return nil, fmt.Errorf("invalid repository")
	}
	return &GitHub{Repository: repo, Client: &http.Client{Timeout: 10 * time.Minute, CheckRedirect: func(req *http.Request, via []*http.Request) error {
		if len(via) >= 5 || !safeGitHubURL(req.URL.String()) {
			return fmt.Errorf("untrusted download redirect")
		}
		return nil
	}}}, nil
}
func (g *GitHub) get(ctx context.Context, u string, max int64, out io.Writer) error {
	if !safeGitHubURL(u) {
		return fmt.Errorf("untrusted asset URL")
	}
	req, e := http.NewRequestWithContext(ctx, "GET", u, nil)
	if e != nil {
		return e
	}
	req.Header.Set("User-Agent", "SmartDesktopMimi-Updater/"+Version)
	req.Header.Set("Accept", "application/vnd.github+json")
	res, e := g.Client.Do(req)
	if e != nil {
		return e
	}
	defer res.Body.Close()
	if res.StatusCode != 200 {
		return fmt.Errorf("GitHub HTTP %d (rate limit/offline: retain current version)", res.StatusCode)
	}
	if res.ContentLength > max {
		return fmt.Errorf("response too large")
	}
	n, e := io.Copy(out, io.LimitReader(res.Body, max+1))
	if e != nil {
		return e
	}
	if n > max {
		return fmt.Errorf("response exceeded size limit")
	}
	return nil
}
func (g *GitHub) bytes(ctx context.Context, u string, max int64) ([]byte, error) {
	var b strings.Builder
	e := g.get(ctx, u, max, &b)
	return []byte(b.String()), e
}
func (g *GitHub) Check(ctx context.Context, d Device, trust Trust, channel, current string, floor uint64) (*Candidate, error) {
	if e := d.Validate(); e != nil {
		return nil, e
	}
	if channel != "stable" && channel != "testing" {
		return nil, fmt.Errorf("unknown update channel")
	}
	var best *Candidate
	for page := 1; page <= 10; page++ {
		raw, e := g.bytes(ctx, fmt.Sprintf("https://api.github.com/repos/%s/releases?per_page=100&page=%d", g.Repository, page), 4<<20)
		if e != nil {
			return nil, e
		}
		var releases []Release
		if e = json.Unmarshal(raw, &releases); e != nil {
			return nil, e
		}
		for _, r := range releases {
			kind, _, _ := releaseIdentity(r.Tag)
			if kind != "application" {
				continue
			}
			if r.Draft || (channel == "stable" && r.Prerelease) {
				continue
			}
			assets := map[string]Asset{}
			duplicate := false
			for _, a := range r.Assets {
				if _, ok := assets[a.Name]; ok {
					duplicate = true
				}
				assets[a.Name] = a
			}
			if duplicate {
				continue
			}
			_, hasIndex := assets["release-index.json"]
			_, hasIndexSig := assets["release-index.sig"]
			if hasIndex || hasIndexSig {
				candidate, err := g.indexedCandidate(ctx, r, assets, d, trust, channel, current, floor)
				if err != nil {
					return nil, err
				} // Never fall back to legacy assets after an index failure.
				if candidate != nil {
					if best == nil {
						best = candidate
					} else {
						cmp, err := Compare(candidate.Manifest.Version, best.Manifest.Version)
						if err != nil {
							return nil, err
						}
						if cmp == 0 && candidate.Manifest.PackageSHA256 != best.Manifest.PackageSHA256 {
							return nil, fmt.Errorf("conflicting release assets")
						}
						if cmp > 0 {
							best = candidate
						}
					}
				}
				continue
			}
			ma, ok := assets["manifest.json"]
			if !ok {
				continue
			}
			sa, ok := assets["manifest.sig"]
			if !ok {
				continue
			}
			pa, ok := assets["application.tar.gz"]
			if !ok {
				continue
			}
			raw, e = g.bytes(ctx, ma.URL, 1<<20)
			if e != nil {
				return nil, e
			}
			sig, e := g.bytes(ctx, sa.URL, 4096)
			if e != nil {
				return nil, e
			}
			m, e := Verify(raw, sig, trust)
			if e != nil {
				continue
			}
			if m.Channel != channel || r.Tag != "v"+m.Version || r.Prerelease != (m.Channel == "testing") || pa.Size != m.PackageBytes || m.Sequence <= floor {
				continue
			}
			if Match(m, d) != nil {
				continue
			}
			c, e := Compare(m.Version, current)
			if e != nil {
				return nil, e
			}
			if c <= 0 {
				continue
			}
			if best == nil {
				best = &Candidate{m, raw, sig, pa}
				continue
			}
			c, e = Compare(m.Version, best.Manifest.Version)
			if e != nil {
				return nil, e
			}
			if c == 0 && m.PackageSHA256 != best.Manifest.PackageSHA256 {
				return nil, fmt.Errorf("conflicting release assets")
			}
			if c > 0 {
				best = &Candidate{m, raw, sig, pa}
			}
		}
		if len(releases) < 100 {
			return best, nil
		}
	}
	return nil, fmt.Errorf("release pagination limit exceeded; refusing incomplete candidate selection")
}
func (g *GitHub) Download(ctx context.Context, c *Candidate, dir string) (string, error) {
	if e := os.MkdirAll(dir, 0700); e != nil {
		return "", e
	}
	var available syscall.Statfs_t
	if e := syscall.Statfs(dir, &available); e != nil {
		return "", e
	}
	need := uint64(c.Manifest.PackageBytes+c.Manifest.ExpandedBytes) + (32 << 20)
	if uint64(available.Bavail)*uint64(available.Bsize) < need {
		return "", fmt.Errorf("insufficient download and staging space")
	}
	f, e := os.CreateTemp(dir, ".download-")
	if e != nil {
		return "", e
	}
	tmp := f.Name()
	defer os.Remove(tmp)
	e = g.get(ctx, c.Package.URL, c.Manifest.PackageBytes, &downloadProgress{writer: f, total: c.Manifest.PackageBytes, report: g.Progress})
	if e == nil {
		e = f.Sync()
	}
	ce := f.Close()
	if e != nil {
		return "", e
	}
	if ce != nil {
		return "", ce
	}
	if e = CheckArchive(tmp, c.Manifest, ""); e != nil {
		return "", e
	}
	dest := filepath.Join(dir, c.Manifest.PackageSHA256+".tar.gz")
	if e = os.Rename(tmp, dest); e != nil {
		return "", e
	}
	return dest, syncDir(dir)
}

type downloadProgress struct {
	writer         io.Writer
	written, total int64
	report         func(int64, int64)
}

func (p *downloadProgress) Write(b []byte) (int, error) {
	n, e := p.writer.Write(b)
	p.written += int64(n)
	if p.report != nil {
		p.report(p.written, p.total)
	}
	return n, e
}
