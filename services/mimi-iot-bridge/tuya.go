// The bridge runs on a trusted server. Tuya project credentials never enter
// the desktop package; each desktop token is restricted to one authorized UID.
package main

import (
	"bytes"
	"context"
	"crypto/hmac"
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"net/http"
	"net/url"
	"regexp"
	"strconv"
	"strings"
	"sync"
	"time"
)

var identifier = regexp.MustCompile(`^[A-Za-z0-9_-]{1,128}$`)
var regions = map[string]bool{"https://openapi.tuyacn.com": true, "https://openapi.tuyaus.com": true, "https://openapi.tuyaeu.com": true, "https://openapi.tuyain.com": true, "https://openapi-ueaz.tuyaus.com": true, "https://openapi-weaz.tuyaeu.com": true}

type Client struct {
	Base, ID, Secret string
	HTTP             *http.Client
	mu               sync.Mutex
	token            string
	expires          time.Time
}
type envelope struct {
	Success bool            `json:"success"`
	Code    json.RawMessage `json:"code"`
	Result  json.RawMessage `json:"result"`
}
type Point struct {
	Code  string          `json:"code"`
	Value json.RawMessage `json:"value"`
}
type Device struct {
	ID       string  `json:"id"`
	Name     string  `json:"name"`
	Category string  `json:"category"`
	Online   bool    `json:"online"`
	Status   []Point `json:"status"`
}
type Function struct {
	Code   string `json:"code"`
	Type   string `json:"type"`
	Values string `json:"values"`
}
type Command struct {
	Code  string          `json:"code"`
	Value json.RawMessage `json:"value"`
}

func signature(id, secret, token, t, method, path string, body []byte) string {
	digest := sha256.Sum256(body)
	canonical := method + "\n" + hex.EncodeToString(digest[:]) + "\n\n" + path
	h := hmac.New(sha256.New, []byte(secret))
	h.Write([]byte(id + token + t + canonical))
	return strings.ToUpper(hex.EncodeToString(h.Sum(nil)))
}
func (c *Client) request(ctx context.Context, method, path, token string, body []byte, out any) error {
	req, err := http.NewRequestWithContext(ctx, method, c.Base+path, bytes.NewReader(body))
	if err != nil {
		return errors.New("invalid cloud request")
	}
	t := strconv.FormatInt(time.Now().UnixMilli(), 10)
	req.Header.Set("client_id", c.ID)
	req.Header.Set("t", t)
	req.Header.Set("sign_method", "HMAC-SHA256")
	req.Header.Set("sign", signature(c.ID, c.Secret, token, t, method, path, body))
	req.Header.Set("Content-Type", "application/json")
	if token != "" {
		req.Header.Set("access_token", token)
	}
	resp, err := c.HTTP.Do(req)
	if err != nil {
		return errors.New("cloud transport failed")
	}
	defer resp.Body.Close()
	if resp.StatusCode != http.StatusOK {
		return fmt.Errorf("cloud HTTP %d", resp.StatusCode)
	}
	raw, err := io.ReadAll(io.LimitReader(resp.Body, 2*1024*1024+1))
	if err != nil || len(raw) > 2*1024*1024 {
		return errors.New("invalid cloud response size")
	}
	var result envelope
	if json.Unmarshal(raw, &result) != nil {
		return errors.New("invalid cloud response")
	}
	if !result.Success {
		return errors.New("cloud authorization or operation rejected")
	}
	if len(result.Result) == 0 || bytes.Equal(result.Result, []byte("null")) {
		return errors.New("missing cloud result")
	}
	return json.Unmarshal(result.Result, out)
}
func (c *Client) access(ctx context.Context) (string, error) {
	c.mu.Lock()
	defer c.mu.Unlock()
	if c.token != "" && time.Now().Before(c.expires) {
		return c.token, nil
	}
	var result struct {
		Token   string `json:"access_token"`
		Expires int    `json:"expire_time"`
	}
	if err := c.request(ctx, "GET", "/v1.0/token?grant_type=1", "", nil, &result); err != nil {
		return "", err
	}
	if result.Token == "" || result.Expires < 60 || result.Expires > 86400 {
		return "", errors.New("invalid cloud token lifetime")
	}
	c.token = result.Token
	c.expires = time.Now().Add(time.Duration(result.Expires-30) * time.Second)
	return c.token, nil
}
func (c *Client) call(ctx context.Context, method, path string, body []byte, out any) error {
	token, err := c.access(ctx)
	if err != nil {
		return err
	}
	return c.request(ctx, method, path, token, body, out)
}
func (c *Client) Devices(ctx context.Context, uid string) ([]Device, error) {
	if !identifier.MatchString(uid) {
		return nil, errors.New("invalid account identifier")
	}
	result := []Device{}
	seen := map[string]bool{}
	// Explicit pagination; no unbounded cloud account enumeration.
	for page := 1; page <= 20; page++ {
		var batch []Device
		q := url.Values{"page_no": {strconv.Itoa(page)}, "page_size": {"100"}}
		if err := c.call(ctx, "GET", "/v1.0/users/"+uid+"/devices?"+q.Encode(), nil, &batch); err != nil {
			return nil, err
		}
		for _, d := range batch {
			if !identifier.MatchString(d.ID) || seen[d.ID] {
				return nil, errors.New("invalid or repeated cloud device")
			}
			seen[d.ID] = true
			result = append(result, d)
		}
		if len(batch) < 100 {
			return result, nil
		}
	}
	return nil, errors.New("device list exceeds supported page limit")
}
func (c *Client) Functions(ctx context.Context, id string) ([]Function, error) {
	if !identifier.MatchString(id) {
		return nil, errors.New("invalid device identifier")
	}
	var result struct {
		Functions []Function `json:"functions"`
	}
	err := c.call(ctx, "GET", "/v1.0/devices/"+id+"/functions", nil, &result)
	return result.Functions, err
}
func validateCommand(command Command, functions []Function) error {
	if !identifier.MatchString(command.Code) {
		return errors.New("invalid command code")
	}
	command.Value = bytes.TrimSpace(command.Value)
	if len(command.Value) == 0 || bytes.Equal(command.Value, []byte("null")) {
		return errors.New("a concrete command value is required")
	}
	for _, f := range functions {
		if command.Code != f.Code {
			continue
		}
		switch strings.ToLower(f.Type) {
		case "boolean":
			var value bool
			if json.Unmarshal(command.Value, &value) != nil || bytes.Equal(command.Value, []byte("null")) {
				return errors.New("boolean value required")
			}
			return nil
		case "integer":
			var value int64
			var limits struct{ Min, Max, Step int64 }
			if json.Unmarshal(command.Value, &value) != nil || json.Unmarshal([]byte(f.Values), &limits) != nil || limits.Step <= 0 || limits.Max < limits.Min || limits.Min < -9007199254740991 || limits.Max > 9007199254740991 {
				return errors.New("invalid numeric command schema")
			}
			if value < limits.Min || value > limits.Max || (value-limits.Min)%limits.Step != 0 {
				return errors.New("numeric command outside device bounds")
			}
			return nil
		case "enum":
			var value string
			var options struct {
				Range []string `json:"range"`
			}
			if json.Unmarshal(command.Value, &value) != nil || json.Unmarshal([]byte(f.Values), &options) != nil {
				return errors.New("invalid enum command")
			}
			for _, option := range options.Range {
				if value == option {
					return nil
				}
			}
			return errors.New("unsupported enum value")
		default:
			return errors.New("this command type is not enabled")
		}
	}
	return errors.New("device does not advertise this command")
}
func (c *Client) Send(ctx context.Context, id string, command Command) error {
	functions, err := c.Functions(ctx, id)
	if err != nil {
		return err
	}
	if err = validateCommand(command, functions); err != nil {
		return err
	}
	body, err := json.Marshal(struct {
		Commands []Command `json:"commands"`
	}{[]Command{command}})
	if err != nil {
		return err
	}
	var accepted bool
	if err = c.call(ctx, "POST", "/v1.0/devices/"+id+"/commands", body, &accepted); err != nil {
		return err
	}
	if !accepted {
		return errors.New("device command rejected")
	}
	return nil
}
