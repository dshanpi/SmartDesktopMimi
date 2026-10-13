package main

import (
	"context"
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"fmt"
	"net/http"
	"net/http/httptest"
	"os"
	"path/filepath"
	"strings"
	"sync/atomic"
	"testing"
)

func TestSignatureVector(t *testing.T) {
	if got := signature("fixture-client", "fixture-only", "", "1700000000000", "GET", "/v1.0/token?grant_type=1", nil); got != "A0950053F268813D477551394D7CB6D67B890CF1ACC33E1D9D32CEF1377F9D41" {
		t.Fatal(got)
	}
}
func TestCommandValidation(t *testing.T) {
	f := []Function{{"switch", "Boolean", "{}"}, {"brightness", "Integer", `{"min":10,"max":1000,"step":10}`}, {"mode", "Enum", `{"range":["white","colour"]}`}, {"raw", "Raw", "{}"}}
	for _, tc := range []struct {
		code, value string
		ok          bool
	}{{"switch", "true", true}, {"switch", " null ", false}, {"switch", "1", false}, {"brightness", "10", true}, {"brightness", "11", false}, {"brightness", "1001", false}, {"brightness", "1e2", false}, {"mode", `"white"`, true}, {"mode", `"unknown"`, false}, {"mode", "null", false}, {"missing", "true", false}, {"raw", `"1234"`, false}} {
		t.Run(tc.code+tc.value, func(t *testing.T) {
			if ok := validateCommand(Command{tc.code, json.RawMessage(tc.value)}, f) == nil; ok != tc.ok {
				t.Fatalf("accepted=%v", ok)
			}
		})
	}
	if validateCommand(Command{"x", json.RawMessage("0")}, []Function{{"x", "Integer", `{"min":-9223372036854775808,"max":9223372036854775807,"step":1}`}}) == nil {
		t.Fatal("unsafe arithmetic bounds accepted")
	}
}
func fixture(t *testing.T, online bool, category string) (*Client, *atomic.Int32) {
	t.Helper()
	posts := &atomic.Int32{}
	s := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		w.Header().Set("Content-Type", "application/json")
		switch {
		case r.URL.Path == "/v1.0/token":
			fmt.Fprint(w, `{"success":true,"result":{"access_token":"fixture-access","expire_time":3600}}`)
		case strings.Contains(r.URL.Path, "/users/"):
			fmt.Fprintf(w, `{"success":true,"result":[{"id":"lamp1","name":"Fixture lamp","category":%q,"online":%v,"local_key":"MUST-NOT-LEAK","ip":"192.0.2.1","status":[{"code":"switch","value":false}]}]}`, category, online)
		case strings.HasSuffix(r.URL.Path, "/functions"):
			fmt.Fprint(w, `{"success":true,"result":{"functions":[{"code":"switch","type":"Boolean","values":"{}"}]}}`)
		case strings.HasSuffix(r.URL.Path, "/commands"):
			posts.Add(1)
			fmt.Fprint(w, `{"success":true,"result":true}`)
		default:
			http.NotFound(w, r)
		}
	}))
	t.Cleanup(s.Close)
	return &Client{Base: s.URL, ID: "fixture", Secret: "fixture", HTTP: s.Client()}, posts
}
func authAPI(c *Client) *API {
	digest := sha256.Sum256([]byte(strings.Repeat("x", 32)))
	return &API{Cloud: c, Grants: map[string]Grant{hex.EncodeToString(digest[:]): {UID: "user1", ControlCategories: []string{"dj"}}}}
}
func perform(a *API, method, path, body string, auth bool) *httptest.ResponseRecorder {
	r := httptest.NewRequest(method, path, strings.NewReader(body))
	if auth {
		r.Header.Set("Authorization", "Bearer "+strings.Repeat("x", 32))
	}
	w := httptest.NewRecorder()
	a.ServeHTTP(w, r)
	return w
}
func TestAuthorizationScopeAndReadback(t *testing.T) {
	c, posts := fixture(t, true, "dj")
	w := perform(authAPI(c), "GET", "/v1/devices", "", false)
	if w.Code != 401 {
		t.Fatal(w.Code)
	}
	w = perform(authAPI(c), "GET", "/v1/devices", "", true)
	if w.Code != 200 || strings.Contains(w.Body.String(), "MUST-NOT-LEAK") || strings.Contains(w.Body.String(), "192.0.2.1") {
		t.Fatal(w.Code, w.Body.String())
	}
	w = perform(authAPI(c), "POST", "/v1/devices/foreign/commands", `{"code":"switch","value":true}`, true)
	if w.Code != 404 || posts.Load() != 0 {
		t.Fatal("cross-account control")
	}
	w = perform(authAPI(c), "POST", "/v1/devices/lamp1/commands", `{"code":"switch","value":true}`, true)
	if w.Code != 200 || posts.Load() != 1 || !strings.Contains(w.Body.String(), `"state_confirmed":false`) {
		t.Fatal(w.Code, w.Body.String())
	}
	w = perform(authAPI(c), "POST", "/v1/devices/lamp1/commands", `{"code":"switch","value":true,"force":true}`, true)
	if w.Code != 400 || posts.Load() != 1 {
		t.Fatal("unknown parameter accepted")
	}
}
func TestOfflineReadonlyAndRateLimit(t *testing.T) {
	for _, tc := range []struct {
		online   bool
		category string
		status   int
	}{{false, "dj", 409}, {true, "lock", 403}} {
		c, posts := fixture(t, tc.online, tc.category)
		w := perform(authAPI(c), "POST", "/v1/devices/lamp1/commands", `{"code":"switch","value":true}`, true)
		if w.Code != tc.status || posts.Load() != 0 {
			t.Fatal(w.Code)
		}
	}
	c, _ := fixture(t, true, "dj")
	a := authAPI(c)
	perform(a, "GET", "/v1/devices", "", true)
	if perform(a, "GET", "/v1/devices", "", true).Code != 429 {
		t.Fatal("rate limit")
	}
}
func TestCloudFailureDoesNotRetryCommand(t *testing.T) {
	var posts atomic.Int32
	s := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if r.Method == "POST" {
			posts.Add(1)
		}
		http.Error(w, "unavailable", 503)
	}))
	defer s.Close()
	c := &Client{Base: s.URL, HTTP: s.Client()}
	err := c.request(context.Background(), "POST", "/commands", "", []byte(`{}`), new(bool))
	if err == nil || posts.Load() != 1 {
		t.Fatal("command retried or error lost")
	}
}
func TestPrivateConfiguration(t *testing.T) {
	d := t.TempDir()
	p := filepath.Join(d, "config.json")
	digest := sha256.Sum256([]byte(strings.Repeat("x", 32)))
	content := fmt.Sprintf(`{"endpoint":"https://openapi.tuyacn.com","access_id":"fixture","access_secret":"fixture","token_sha256_grants":{%q:{"uid":"user1","control_categories":["dj"]}}}`, hex.EncodeToString(digest[:]))
	if err := os.WriteFile(p, []byte(content), 0600); err != nil {
		t.Fatal(err)
	}
	if _, err := loadConfig(p); err != nil {
		t.Fatal(err)
	}
	os.Chmod(p, 0644)
	if _, err := loadConfig(p); err == nil {
		t.Fatal("public credentials accepted")
	}
	os.Chmod(p, 0600)
	link := filepath.Join(d, "link")
	os.Symlink(p, link)
	if _, err := loadConfig(link); err == nil {
		t.Fatal("symlink accepted")
	}
}
