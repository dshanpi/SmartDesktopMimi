package kvm

import (
	"bytes"
	"encoding/json"
	"mime/multipart"
	"net/http"
	"net/http/httptest"
	"os"
	"path/filepath"
	"strings"
	"testing"

	"github.com/gin-gonic/gin"
)

func platformAppTestRouter(t *testing.T, script string) *gin.Engine {
	t.Helper()
	appctl := filepath.Join(t.TempDir(), "aitvbox-appctl")
	if err := os.WriteFile(appctl, []byte(script), 0700); err != nil {
		t.Fatal(err)
	}
	t.Setenv("AITVBOX_APPCTL", appctl)
	router := platformTestRouter(t)
	config.LocalAuthMode = "password"
	config.LocalAuthToken = "app-test-token"
	return router
}

func authorizeAppRequest(request *http.Request) {
	request.AddCookie(&http.Cookie{Name: "authToken", Value: "app-test-token"})
}

func platformAdminTestRouter(t *testing.T, script string) *gin.Engine {
	t.Helper()
	adminctl := filepath.Join(t.TempDir(), "aitvbox-adminctl")
	if err := os.WriteFile(adminctl, []byte(script), 0700); err != nil {
		t.Fatal(err)
	}
	t.Setenv("AITVBOX_ADMINCTL", adminctl)
	router := platformTestRouter(t)
	config.LocalAuthMode = "password"
	config.LocalAuthToken = "app-test-token"
	return router
}

func TestPlatformAppRoutesRequireAuthentication(t *testing.T) {
	marker := filepath.Join(t.TempDir(), "called")
	t.Setenv("APPCTL_MARKER", marker)
	router := platformAppTestRouter(t, `#!/bin/sh
touch "$APPCTL_MARKER"
exit 1
`)
	request := httptest.NewRequest(http.MethodGet, "/api/apps", nil)
	response := httptest.NewRecorder()
	router.ServeHTTP(response, request)
	if response.Code != http.StatusUnauthorized {
		t.Fatalf("status = %d, want 401", response.Code)
	}
	if _, err := os.Stat(marker); !os.IsNotExist(err) {
		t.Fatalf("application manager ran without authentication: %v", err)
	}
}

func TestPlatformAppList(t *testing.T) {
	router := platformAppTestRouter(t, `#!/bin/sh
[ "$1" = list ] || exit 2
printf '%s\n' com.example.alpha 1.2.3 org.demo.beta 4.5.6
`)
	request := httptest.NewRequest(http.MethodGet, "/api/apps", nil)
	authorizeAppRequest(request)
	response := httptest.NewRecorder()
	router.ServeHTTP(response, request)
	if response.Code != http.StatusOK {
		t.Fatalf("status = %d, body=%s", response.Code, response.Body.String())
	}
	var payload struct {
		Apps []installedApp `json:"apps"`
	}
	if err := json.Unmarshal(response.Body.Bytes(), &payload); err != nil {
		t.Fatal(err)
	}
	if len(payload.Apps) != 2 ||
		payload.Apps[0].ID != "com.example.alpha" ||
		payload.Apps[1].Version != "4.5.6" {
		t.Fatalf("unexpected application list: %#v", payload.Apps)
	}
}

func TestPlatformAppInstallAndTemporaryCleanup(t *testing.T) {
	logPath := filepath.Join(t.TempDir(), "arguments")
	t.Setenv("APPCTL_LOG", logPath)
	router := platformAppTestRouter(t, `#!/bin/sh
printf '%s\n' "$@" >"$APPCTL_LOG"
[ "$1" = install ] && [ "$2" = --replace ] && [ -f "$3" ] || exit 2
printf '%s\n' "OK com.example.alpha 1.2.3"
`)

	var body bytes.Buffer
	writer := multipart.NewWriter(&body)
	part, err := writer.CreateFormFile("package", "alpha.aitapp")
	if err != nil {
		t.Fatal(err)
	}
	if _, err := part.Write([]byte("signed-package-placeholder")); err != nil {
		t.Fatal(err)
	}
	if err := writer.Close(); err != nil {
		t.Fatal(err)
	}
	request := httptest.NewRequest(
		http.MethodPost, "/api/apps/install?mode=replace", &body,
	)
	request.Header.Set("Content-Type", writer.FormDataContentType())
	authorizeAppRequest(request)
	response := httptest.NewRecorder()
	router.ServeHTTP(response, request)
	if response.Code != http.StatusCreated {
		t.Fatalf("status = %d, body=%s", response.Code, response.Body.String())
	}
	arguments, err := os.ReadFile(logPath)
	if err != nil {
		t.Fatal(err)
	}
	lines := strings.Fields(string(arguments))
	if len(lines) != 3 || lines[0] != "install" || lines[1] != "--replace" {
		t.Fatalf("unexpected appctl arguments: %q", arguments)
	}
	if _, err := os.Stat(lines[2]); !os.IsNotExist(err) {
		t.Fatalf("temporary upload was not removed: %v", err)
	}
}

func TestPlatformAppInstallRejectsUnexpectedParts(t *testing.T) {
	router := platformAppTestRouter(t, "#!/bin/sh\nexit 99\n")
	var body bytes.Buffer
	writer := multipart.NewWriter(&body)
	if err := writer.WriteField("package", "not-a-file"); err != nil {
		t.Fatal(err)
	}
	if err := writer.Close(); err != nil {
		t.Fatal(err)
	}
	request := httptest.NewRequest(http.MethodPost, "/api/apps/install", &body)
	request.Header.Set("Content-Type", writer.FormDataContentType())
	authorizeAppRequest(request)
	response := httptest.NewRecorder()
	router.ServeHTTP(response, request)
	if response.Code != http.StatusBadRequest {
		t.Fatalf("status = %d, want 400; body=%s", response.Code, response.Body.String())
	}
}

func TestPlatformAppRemoveValidatesID(t *testing.T) {
	marker := filepath.Join(t.TempDir(), "called")
	t.Setenv("APPCTL_MARKER", marker)
	router := platformAppTestRouter(t, `#!/bin/sh
touch "$APPCTL_MARKER"
`)
	request := httptest.NewRequest(
		http.MethodDelete, "/api/apps/invalid-id", nil,
	)
	authorizeAppRequest(request)
	response := httptest.NewRecorder()
	router.ServeHTTP(response, request)
	if response.Code != http.StatusBadRequest {
		t.Fatalf("status = %d, want 400", response.Code)
	}
	if _, err := os.Stat(marker); !os.IsNotExist(err) {
		t.Fatalf("application manager ran for invalid ID: %v", err)
	}
}

func TestPlatformPublisherKeyList(t *testing.T) {
	router := platformAdminTestRouter(t, `#!/bin/sh
[ "$1" = list-app-keys ] || exit 2
printf '%s %064d\n' example-dev.pem 0
`)
	request := httptest.NewRequest(http.MethodGet, "/api/apps/keys", nil)
	authorizeAppRequest(request)
	response := httptest.NewRecorder()
	router.ServeHTTP(response, request)
	if response.Code != http.StatusOK {
		t.Fatalf("status = %d, body=%s", response.Code, response.Body.String())
	}
	var payload struct {
		Keys []appPublisherKey `json:"keys"`
	}
	if err := json.Unmarshal(response.Body.Bytes(), &payload); err != nil {
		t.Fatal(err)
	}
	if len(payload.Keys) != 1 || payload.Keys[0].Name != "example-dev" ||
		len(payload.Keys[0].Fingerprint) != 64 {
		t.Fatalf("unexpected publisher key list: %#v", payload.Keys)
	}
}

func TestPlatformPublisherKeyAddAndTemporaryCleanup(t *testing.T) {
	logPath := filepath.Join(t.TempDir(), "arguments")
	t.Setenv("ADMINCTL_LOG", logPath)
	router := platformAdminTestRouter(t, `#!/bin/sh
printf '%s\n' "$@" >"$ADMINCTL_LOG"
[ "$1" = add-app-key ] && [ "$2" = example-dev ] && [ -f "$3" ] || exit 2
printf '%s\n' "OK app-key=example-dev"
`)
	var body bytes.Buffer
	writer := multipart.NewWriter(&body)
	part, err := writer.CreateFormFile("key", "developer.pem")
	if err != nil {
		t.Fatal(err)
	}
	if _, err := part.Write([]byte("PUBLIC KEY")); err != nil {
		t.Fatal(err)
	}
	if err := writer.Close(); err != nil {
		t.Fatal(err)
	}
	request := httptest.NewRequest(
		http.MethodPost, "/api/apps/keys?name=example-dev", &body,
	)
	request.Header.Set("Content-Type", writer.FormDataContentType())
	authorizeAppRequest(request)
	response := httptest.NewRecorder()
	router.ServeHTTP(response, request)
	if response.Code != http.StatusCreated {
		t.Fatalf("status = %d, body=%s", response.Code, response.Body.String())
	}
	arguments, err := os.ReadFile(logPath)
	if err != nil {
		t.Fatal(err)
	}
	lines := strings.Fields(string(arguments))
	if len(lines) != 3 || lines[0] != "add-app-key" ||
		lines[1] != "example-dev" {
		t.Fatalf("unexpected adminctl arguments: %q", arguments)
	}
	if _, err := os.Stat(lines[2]); !os.IsNotExist(err) {
		t.Fatalf("temporary public key was not removed: %v", err)
	}
}
