package kvm

import (
	"context"
	"errors"
	"fmt"
	"io"
	"net/http"
	"os"
	"os/exec"
	"regexp"
	"strings"
	"time"

	"github.com/gin-gonic/gin"
)

const (
	maxAppPackageBytes = int64(32 * 1024 * 1024)
	maxAppRequestBytes = maxAppPackageBytes + int64(1024*1024)
)

var appIDPattern = regexp.MustCompile(
	`^[a-z][a-z0-9]*(\.[a-z0-9][a-z0-9-]*)+$`,
)
var appKeyNamePattern = regexp.MustCompile(`^[A-Za-z0-9][A-Za-z0-9._-]{0,63}$`)
var sha256Pattern = regexp.MustCompile(`^[0-9a-f]{64}$`)

type installedApp struct {
	ID      string `json:"id"`
	Version string `json:"version"`
}

type appPublisherKey struct {
	Name        string `json:"name"`
	Fingerprint string `json:"fingerprint"`
}

func appctlExecutable() string {
	if path := os.Getenv("AITVBOX_APPCTL"); path != "" {
		return path
	}
	return "/usr/bin/aitvbox-appctl"
}

func adminctlExecutable() string {
	if path := os.Getenv("AITVBOX_ADMINCTL"); path != "" {
		return path
	}
	return "/usr/bin/aitvbox-adminctl"
}

func appctlOutput(ctx context.Context, arguments ...string) (string, error) {
	output, err := exec.CommandContext(
		ctx, appctlExecutable(), arguments...,
	).CombinedOutput()
	message := strings.TrimSpace(string(output))
	if len(message) > 4096 {
		message = message[:4096]
	}
	return message, err
}

func adminctlOutput(ctx context.Context, arguments ...string) (string, error) {
	output, err := exec.CommandContext(
		ctx, adminctlExecutable(), arguments...,
	).CombinedOutput()
	message := strings.TrimSpace(string(output))
	if len(message) > 4096 {
		message = message[:4096]
	}
	return message, err
}

func appctlFailure(c *gin.Context, ctx context.Context, output string, err error, clientError bool) {
	if errors.Is(ctx.Err(), context.DeadlineExceeded) {
		c.JSON(http.StatusGatewayTimeout, gin.H{"error": "Application operation timed out"})
		return
	}
	var executableError *exec.Error
	if errors.As(err, &executableError) || errors.Is(err, os.ErrNotExist) {
		c.JSON(http.StatusServiceUnavailable, gin.H{
			"error": "Application manager is unavailable",
		})
		return
	}
	if output == "" {
		output = "Application operation failed"
	}
	status := http.StatusInternalServerError
	if clientError {
		status = http.StatusBadRequest
	}
	c.JSON(status, gin.H{"error": output})
}

func handleAppList(c *gin.Context) {
	ctx, cancel := context.WithTimeout(c.Request.Context(), 15*time.Second)
	defer cancel()
	output, err := appctlOutput(ctx, "list")
	if err != nil {
		appctlFailure(c, ctx, output, err, false)
		return
	}

	lines := strings.Fields(output)
	if len(lines)%2 != 0 {
		c.JSON(http.StatusInternalServerError, gin.H{
			"error": "Application manager returned an invalid list",
		})
		return
	}
	apps := make([]installedApp, 0, len(lines)/2)
	for index := 0; index < len(lines); index += 2 {
		if !appIDPattern.MatchString(lines[index]) {
			c.JSON(http.StatusInternalServerError, gin.H{
				"error": "Application manager returned an invalid application ID",
			})
			return
		}
		apps = append(apps, installedApp{
			ID:      lines[index],
			Version: lines[index+1],
		})
	}
	c.JSON(http.StatusOK, gin.H{"apps": apps})
}

func handleAppInstall(c *gin.Context) {
	mode := c.Query("mode")
	installArguments := []string{"install"}
	switch mode {
	case "", "upgrade":
	case "replace":
		installArguments = append(installArguments, "--replace")
	case "downgrade":
		installArguments = append(installArguments, "--allow-downgrade")
	default:
		c.JSON(http.StatusBadRequest, gin.H{"error": "Invalid installation mode"})
		return
	}

	c.Request.Body = http.MaxBytesReader(
		c.Writer, c.Request.Body, maxAppRequestBytes,
	)
	reader, err := c.Request.MultipartReader()
	if err != nil {
		c.JSON(http.StatusBadRequest, gin.H{
			"error": "Expected a multipart application package",
		})
		return
	}

	var packagePath string
	defer func() {
		if packagePath != "" {
			_ = os.Remove(packagePath)
		}
	}()
	for {
		part, nextErr := reader.NextPart()
		if errors.Is(nextErr, io.EOF) {
			break
		}
		if nextErr != nil {
			var sizeError *http.MaxBytesError
			if errors.As(nextErr, &sizeError) {
				c.JSON(http.StatusRequestEntityTooLarge, gin.H{
					"error": "Application package exceeds 32 MiB",
				})
			} else {
				c.JSON(http.StatusBadRequest, gin.H{
					"error": "Cannot read multipart application package",
				})
			}
			return
		}
		if part.FormName() != "package" || part.FileName() == "" || packagePath != "" {
			_ = part.Close()
			c.JSON(http.StatusBadRequest, gin.H{
				"error": "Exactly one package file is required",
			})
			return
		}

		temporary, createErr := os.CreateTemp("", "aitvbox-app-upload-*.aitapp")
		if createErr != nil {
			_ = part.Close()
			c.JSON(http.StatusInternalServerError, gin.H{
				"error": "Cannot create temporary application package",
			})
			return
		}
		packagePath = temporary.Name()
		if chmodErr := temporary.Chmod(0600); chmodErr != nil {
			_ = part.Close()
			_ = temporary.Close()
			c.JSON(http.StatusInternalServerError, gin.H{
				"error": "Cannot secure temporary application package",
			})
			return
		}
		written, copyErr := io.Copy(
			temporary, io.LimitReader(part, maxAppPackageBytes+1),
		)
		closeErr := temporary.Close()
		_ = part.Close()
		if written > maxAppPackageBytes {
			c.JSON(http.StatusRequestEntityTooLarge, gin.H{
				"error": "Application package exceeds 32 MiB",
			})
			return
		}
		if copyErr != nil || closeErr != nil {
			c.JSON(http.StatusBadRequest, gin.H{
				"error": "Cannot save application package",
			})
			return
		}
	}
	if packagePath == "" {
		c.JSON(http.StatusBadRequest, gin.H{"error": "Application package is missing"})
		return
	}

	installArguments = append(installArguments, packagePath)
	ctx, cancel := context.WithTimeout(c.Request.Context(), 2*time.Minute)
	defer cancel()
	output, err := appctlOutput(ctx, installArguments...)
	if err != nil {
		appctlFailure(c, ctx, output, err, true)
		return
	}
	fields := strings.Fields(output)
	if len(fields) != 3 || fields[0] != "OK" ||
		!appIDPattern.MatchString(fields[1]) {
		c.JSON(http.StatusInternalServerError, gin.H{
			"error": "Application manager returned an invalid install result",
		})
		return
	}
	c.JSON(http.StatusCreated, gin.H{
		"message": "Application installed",
		"app": installedApp{
			ID:      fields[1],
			Version: fields[2],
		},
	})
}

func handleAppRemove(c *gin.Context) {
	appID := c.Param("id")
	if !appIDPattern.MatchString(appID) || len(appID) > 96 {
		c.JSON(http.StatusBadRequest, gin.H{"error": "Invalid application ID"})
		return
	}
	ctx, cancel := context.WithTimeout(c.Request.Context(), 30*time.Second)
	defer cancel()
	output, err := appctlOutput(ctx, "remove", appID)
	if err != nil {
		appctlFailure(c, ctx, output, err, true)
		return
	}
	c.JSON(http.StatusOK, gin.H{
		"message": fmt.Sprintf("Application %s removed", appID),
	})
}

func handleAppKeyList(c *gin.Context) {
	ctx, cancel := context.WithTimeout(c.Request.Context(), 15*time.Second)
	defer cancel()
	output, err := adminctlOutput(ctx, "list-app-keys")
	if err != nil {
		appctlFailure(c, ctx, output, err, false)
		return
	}
	fields := strings.Fields(output)
	if len(fields)%2 != 0 {
		c.JSON(http.StatusInternalServerError, gin.H{
			"error": "Administrator tool returned an invalid publisher key list",
		})
		return
	}
	keys := make([]appPublisherKey, 0, len(fields)/2)
	for index := 0; index < len(fields); index += 2 {
		name := strings.TrimSuffix(fields[index], ".pem")
		if name == fields[index] || !appKeyNamePattern.MatchString(name) ||
			!sha256Pattern.MatchString(fields[index+1]) {
			c.JSON(http.StatusInternalServerError, gin.H{
				"error": "Administrator tool returned an invalid publisher key",
			})
			return
		}
		keys = append(keys, appPublisherKey{
			Name:        name,
			Fingerprint: fields[index+1],
		})
	}
	c.JSON(http.StatusOK, gin.H{"keys": keys})
}

func handleAppKeyAdd(c *gin.Context) {
	name := c.Query("name")
	if !appKeyNamePattern.MatchString(name) {
		c.JSON(http.StatusBadRequest, gin.H{"error": "Invalid publisher key name"})
		return
	}
	c.Request.Body = http.MaxBytesReader(c.Writer, c.Request.Body, 64*1024)
	if err := c.Request.ParseMultipartForm(32 * 1024); err != nil {
		var sizeError *http.MaxBytesError
		if errors.As(err, &sizeError) {
			c.JSON(http.StatusRequestEntityTooLarge, gin.H{
				"error": "Publisher public key is too large",
			})
		} else {
			c.JSON(http.StatusBadRequest, gin.H{
				"error": "Expected a multipart publisher public key",
			})
		}
		return
	}
	defer func() {
		if c.Request.MultipartForm != nil {
			_ = c.Request.MultipartForm.RemoveAll()
		}
	}()
	files := c.Request.MultipartForm.File["key"]
	if len(files) != 1 || len(c.Request.MultipartForm.File) != 1 ||
		len(c.Request.MultipartForm.Value) != 0 {
		c.JSON(http.StatusBadRequest, gin.H{
			"error": "Exactly one publisher public key is required",
		})
		return
	}
	source, err := files[0].Open()
	if err != nil {
		c.JSON(http.StatusBadRequest, gin.H{"error": "Cannot read publisher public key"})
		return
	}
	defer source.Close()
	temporary, err := os.CreateTemp("", "aitvbox-app-key-*.pem")
	if err != nil {
		c.JSON(http.StatusInternalServerError, gin.H{
			"error": "Cannot create temporary publisher public key",
		})
		return
	}
	path := temporary.Name()
	defer os.Remove(path)
	if err := temporary.Chmod(0600); err != nil {
		_ = temporary.Close()
		c.JSON(http.StatusInternalServerError, gin.H{
			"error": "Cannot secure temporary publisher public key",
		})
		return
	}
	written, copyErr := io.Copy(temporary, io.LimitReader(source, 16*1024+1))
	closeErr := temporary.Close()
	if written > 16*1024 {
		c.JSON(http.StatusRequestEntityTooLarge, gin.H{
			"error": "Publisher public key exceeds 16 KiB",
		})
		return
	}
	if copyErr != nil || closeErr != nil {
		c.JSON(http.StatusBadRequest, gin.H{"error": "Cannot save publisher public key"})
		return
	}
	ctx, cancel := context.WithTimeout(c.Request.Context(), 30*time.Second)
	defer cancel()
	output, err := adminctlOutput(ctx, "add-app-key", name, path)
	if err != nil {
		appctlFailure(c, ctx, output, err, true)
		return
	}
	c.JSON(http.StatusCreated, gin.H{
		"message": "Publisher key added",
		"result":  output,
	})
}

func handleAppKeyRemove(c *gin.Context) {
	name := c.Param("name")
	if !appKeyNamePattern.MatchString(name) {
		c.JSON(http.StatusBadRequest, gin.H{"error": "Invalid publisher key name"})
		return
	}
	ctx, cancel := context.WithTimeout(c.Request.Context(), 30*time.Second)
	defer cancel()
	output, err := adminctlOutput(ctx, "remove-app-key", name)
	if err != nil {
		appctlFailure(c, ctx, output, err, true)
		return
	}
	c.JSON(http.StatusOK, gin.H{"message": "Publisher key removed"})
}
