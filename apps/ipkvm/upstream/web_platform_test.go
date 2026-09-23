package kvm

import (
	"encoding/json"
	"net/http"
	"net/http/httptest"
	"os"
	"path/filepath"
	"strings"
	"sync"
	"testing"

	"github.com/gin-gonic/gin"
)

func platformTestRouter(t *testing.T) *gin.Engine {
	t.Helper()
	t.Setenv("AITVBOX_PLATFORM_MODE", "1")
	configPath = filepath.Join(t.TempDir(), "config.json")
	config = nil
	LoadConfig()
	t.Cleanup(func() {
		config = nil
		configLoadError = nil
		configPath = envOrDefault("AITVBOX_IPKVM_CONFIG", "/userdata/kvm_config.json")
	})
	return setupRouter()
}

func TestPlatformRequiresPasswordSetup(t *testing.T) {
	router := platformTestRouter(t)

	body := `{"localAuthMode":"noPassword"}`
	request := httptest.NewRequest(http.MethodPost, "/device/setup", strings.NewReader(body))
	request.Header.Set("Content-Type", "application/json")
	response := httptest.NewRecorder()
	router.ServeHTTP(response, request)
	if response.Code != http.StatusBadRequest {
		t.Fatalf("no-password setup status = %d, want 400", response.Code)
	}

	body = `{"localAuthMode":"password","password":"test-pass-123"}`
	request = httptest.NewRequest(http.MethodPost, "/device/setup", strings.NewReader(body))
	request.Header.Set("Content-Type", "application/json")
	response = httptest.NewRecorder()
	router.ServeHTTP(response, request)
	if response.Code != http.StatusOK {
		t.Fatalf("password setup status = %d, body=%s", response.Code, response.Body.String())
	}
	cookies := response.Result().Cookies()
	if len(cookies) == 0 || !cookies[0].HttpOnly ||
		cookies[0].SameSite != http.SameSiteStrictMode {
		t.Fatalf("auth cookie flags are incomplete: %#v", cookies)
	}
	info, err := os.Stat(configPath)
	if err != nil {
		t.Fatal(err)
	}
	if info.Mode().Perm() != 0600 {
		t.Fatalf("config mode = %o, want 600", info.Mode().Perm())
	}
	temporary, err := filepath.Glob(filepath.Join(
		filepath.Dir(configPath), "."+filepath.Base(configPath)+".tmp.*",
	))
	if err != nil {
		t.Fatal(err)
	}
	if len(temporary) != 0 {
		t.Fatalf("temporary config files remain: %v", temporary)
	}
}

func TestPlatformSetupIsSingleWinner(t *testing.T) {
	router := platformTestRouter(t)
	statuses := make(chan int, 2)
	var requests sync.WaitGroup
	for _, password := range []string{"first-pass-123", "second-pass-123"} {
		requests.Add(1)
		go func(password string) {
			defer requests.Done()
			body := `{"localAuthMode":"password","password":"` + password + `"}`
			request := httptest.NewRequest(
				http.MethodPost, "/device/setup", strings.NewReader(body),
			)
			request.Header.Set("Content-Type", "application/json")
			response := httptest.NewRecorder()
			router.ServeHTTP(response, request)
			statuses <- response.Code
		}(password)
	}
	requests.Wait()
	close(statuses)

	successes := 0
	rejections := 0
	for status := range statuses {
		if status == http.StatusOK {
			successes++
		} else if status == http.StatusBadRequest {
			rejections++
		}
	}
	if successes != 1 || rejections != 1 {
		t.Fatalf("setup results: successes=%d rejections=%d", successes, rejections)
	}
}

func TestPlatformUsesSecureCookieWithExternalTLSFiles(t *testing.T) {
	t.Setenv("AITVBOX_IPKVM_TLS_CERT", "/tmp/test.crt")
	t.Setenv("AITVBOX_IPKVM_TLS_KEY", "/tmp/test.key")
	router := platformTestRouter(t)

	body := `{"localAuthMode":"password","password":"test-pass-123"}`
	request := httptest.NewRequest(http.MethodPost, "/device/setup", strings.NewReader(body))
	request.Header.Set("Content-Type", "application/json")
	response := httptest.NewRecorder()
	router.ServeHTTP(response, request)
	if response.Code != http.StatusOK {
		t.Fatalf("password setup status = %d, body=%s", response.Code, response.Body.String())
	}
	cookies := response.Result().Cookies()
	if len(cookies) == 0 || !cookies[0].Secure {
		t.Fatalf("TLS auth cookie is not Secure: %#v", cookies)
	}
}

func TestPlatformRejectsCrossOriginAndDebugEndpoints(t *testing.T) {
	router := platformTestRouter(t)

	request := httptest.NewRequest(http.MethodGet, "/device/status", nil)
	request.Header.Set("Origin", "http://attacker.invalid")
	response := httptest.NewRecorder()
	router.ServeHTTP(response, request)
	if response.Code != http.StatusForbidden {
		t.Fatalf("cross-origin status = %d, want 403", response.Code)
	}
	request = httptest.NewRequest(http.MethodGet, "/device/status", nil)
	request.Host = "kvm.example"
	request.Header.Set("Origin", "https://kvm.example")
	response = httptest.NewRecorder()
	router.ServeHTTP(response, request)
	if response.Code != http.StatusForbidden {
		t.Fatalf("wrong-scheme origin status = %d, want 403", response.Code)
	}

	for _, path := range []string{
		"/metrics", "/debug/pprof/", "/storage/upload",
	} {
		request = httptest.NewRequest(http.MethodGet, path, nil)
		request.Header.Set("Accept", "application/json")
		response = httptest.NewRecorder()
		router.ServeHTTP(response, request)
		if response.Code != http.StatusNotFound {
			var payload map[string]any
			_ = json.Unmarshal(response.Body.Bytes(), &payload)
			t.Fatalf("%s status = %d, payload=%v", path, response.Code, payload)
		}
	}
}

func TestPlatformAuthenticationMigrationAndRestart(t *testing.T) {
	t.Setenv("AITVBOX_PLATFORM_MODE", "1")
	oldConfigPath := configPath
	configPath = filepath.Join(t.TempDir(), "config.json")
	config = nil
	t.Cleanup(func() {
		config = nil
		configLoadError = nil
		configPath = oldConfigPath
	})
	LoadConfig()
	config.LocalAuthMode = "noPassword"
	config.LocalAuthToken = "persisted-token"
	if err := enforcePlatformAuthentication(); err != nil {
		t.Fatal(err)
	}
	if config.LocalAuthMode != "" || config.LocalAuthToken != "" {
		t.Fatalf("unsafe legacy auth state was retained: %#v", config)
	}

	config.LocalAuthMode = "password"
	config.HashedPassword = "hash"
	config.LocalAuthToken = "persisted-token"
	if err := enforcePlatformAuthentication(); err != nil {
		t.Fatal(err)
	}
	if config.LocalAuthMode != "password" || config.LocalAuthToken != "persisted-token" {
		t.Fatalf("password auth restart state is invalid: %#v", config)
	}
}

func TestPlatformLoginSurvivesWebServiceRestart(t *testing.T) {
	router := platformTestRouter(t)

	setup := httptest.NewRequest(
		http.MethodPost,
		"/device/setup",
		strings.NewReader(`{"localAuthMode":"password","password":"test-pass-123"}`),
	)
	setup.Header.Set("Content-Type", "application/json")
	setupResponse := httptest.NewRecorder()
	router.ServeHTTP(setupResponse, setup)
	if setupResponse.Code != http.StatusOK {
		t.Fatalf("setup status = %d, body=%s", setupResponse.Code, setupResponse.Body.String())
	}

	login := httptest.NewRequest(
		http.MethodPost,
		"/auth/login-local",
		strings.NewReader(`{"password":"test-pass-123"}`),
	)
	login.Header.Set("Content-Type", "application/json")
	loginResponse := httptest.NewRecorder()
	router.ServeHTTP(loginResponse, login)
	if loginResponse.Code != http.StatusOK {
		t.Fatalf("login status = %d, body=%s", loginResponse.Code, loginResponse.Body.String())
	}
	var authCookie *http.Cookie
	for _, cookie := range loginResponse.Result().Cookies() {
		if cookie.Name == "authToken" {
			authCookie = cookie
			break
		}
	}
	if authCookie == nil || authCookie.Value == "" {
		t.Fatal("login did not return an auth cookie")
	}

	// Simulate a new web process loading the same protected configuration.
	config = nil
	LoadConfig()
	if err := enforcePlatformAuthentication(); err != nil {
		t.Fatal(err)
	}
	restartedRouter := setupRouter()
	request := httptest.NewRequest(http.MethodGet, "/device", nil)
	request.AddCookie(authCookie)
	response := httptest.NewRecorder()
	restartedRouter.ServeHTTP(response, request)
	if response.Code != http.StatusOK {
		t.Fatalf("protected request after restart = %d, body=%s", response.Code, response.Body.String())
	}
}

func TestPlatformCorruptAuthenticationConfigFailsClosed(t *testing.T) {
	t.Setenv("AITVBOX_PLATFORM_MODE", "1")
	oldConfigPath := configPath
	configPath = filepath.Join(t.TempDir(), "config.json")
	if err := os.WriteFile(configPath, []byte("{invalid"), 0600); err != nil {
		t.Fatal(err)
	}
	config = nil
	t.Cleanup(func() {
		config = nil
		configLoadError = nil
		configPath = oldConfigPath
	})

	LoadConfig()
	if err := enforcePlatformAuthentication(); err == nil {
		t.Fatal("corrupt authentication config should stop the platform service")
	}
	if _, err := os.Stat(configPath); err != nil {
		t.Fatalf("corrupt config was removed: %v", err)
	}
}

func TestPlatformRPCAllowlist(t *testing.T) {
	t.Setenv("AITVBOX_PLATFORM_MODE", "1")
	allowed, err := DispatchRPCRequest(JSONRPCRequest{
		JSONRPC: "2.0",
		Method:  "ping",
		ID:      1,
	})
	if err != nil || allowed.Error != nil || allowed.Result != "pong" {
		t.Fatalf("ping response = %#v, err=%v", allowed, err)
	}

	denied, err := DispatchRPCRequest(JSONRPCRequest{
		JSONRPC: "2.0",
		Method:  "reboot",
		Params:  map[string]interface{}{"force": true},
		ID:      2,
	})
	if err != nil || denied.Error == nil {
		t.Fatalf("reboot should be rejected: response=%#v err=%v", denied, err)
	}
}
