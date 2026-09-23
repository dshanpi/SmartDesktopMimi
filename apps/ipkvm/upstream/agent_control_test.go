package kvm

import (
	"encoding/json"
	"net"
	"path/filepath"
	"testing"
)

func TestAgentControlStatusAndConfiguration(t *testing.T) {
	socketPath := filepath.Join(t.TempDir(), "control.sock")
	listener, err := net.Listen("unix", socketPath)
	if err != nil {
		t.Fatal(err)
	}
	defer listener.Close()
	t.Setenv("AITVBOX_AGENT_CONTROL_SOCKET", socketPath)

	requests := make(chan map[string]interface{}, 2)
	go func() {
		for index := 0; index < 2; index++ {
			connection, acceptErr := listener.Accept()
			if acceptErr != nil {
				return
			}
			var request map[string]interface{}
			_ = json.NewDecoder(connection).Decode(&request)
			requests <- request
			if request["command"] == "status" {
				_, _ = connection.Write([]byte(
					`{"ok":true,"state":"idle","configured":true,` +
						`"keyConfigured":true,"providerConfigured":true,` +
						`"endpoint":"https://provider.invalid/v1",` +
						`"model":"vision-test","history":[` +
						`{"role":"user","type":"task","message":"safe task"},` +
						`{"role":"tool","type":"hid","step":1,` +
						`"message":"Keyboard report sent"}]}` + "\n",
				))
			} else {
				_, _ = connection.Write([]byte(
					`{"ok":true,"message":"provider configured"}` + "\n",
				))
			}
			_ = connection.Close()
		}
	}()

	status, err := rpcGetAgentStatus()
	if err != nil {
		t.Fatal(err)
	}
	if !status.Configured || status.Endpoint != "https://provider.invalid/v1" ||
		status.Model != "vision-test" {
		t.Fatalf("unexpected status: %#v", status)
	}
	if len(status.History) != 2 ||
		status.History[0].Role != "user" ||
		status.History[1].Step != 1 {
		t.Fatalf("unexpected history: %#v", status.History)
	}
	if _, err := rpcConfigureAgent(
		"https://new-provider.invalid/v1", "new-model",
	); err != nil {
		t.Fatal(err)
	}

	statusRequest := <-requests
	configureRequest := <-requests
	if statusRequest["command"] != "status" {
		t.Fatalf("unexpected status request: %#v", statusRequest)
	}
	if configureRequest["command"] != "configure" ||
		configureRequest["endpoint"] != "https://new-provider.invalid/v1" ||
		configureRequest["model"] != "new-model" {
		t.Fatalf("unexpected configure request: %#v", configureRequest)
	}
}

func TestAgentStartValidatesStepCount(t *testing.T) {
	for _, steps := range []float64{0, 1.5, 31} {
		if _, err := rpcStartAgent("safe task", steps); err == nil {
			t.Fatalf("maxSteps=%v should be rejected", steps)
		}
	}
}
