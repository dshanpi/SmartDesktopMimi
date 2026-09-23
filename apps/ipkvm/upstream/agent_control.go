package kvm

import (
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"net"
	"os/exec"
	"sync"
	"time"
)

const agentControlReplyMax = 96 * 1024

var agentCapture = struct {
	sync.Mutex
	active bool
}{}

type agentControlReply struct {
	OK                 bool                `json:"ok"`
	Message            string              `json:"message,omitempty"`
	State              string              `json:"state,omitempty"`
	Configured         bool                `json:"configured"`
	KeyConfigured      bool                `json:"keyConfigured"`
	ProviderConfigured bool                `json:"providerConfigured"`
	Endpoint           string              `json:"endpoint,omitempty"`
	Model              string              `json:"model,omitempty"`
	Detail             string              `json:"detail,omitempty"`
	History            []agentHistoryEntry `json:"history"`
}

type agentHistoryEntry struct {
	Role    string `json:"role"`
	Type    string `json:"type"`
	Step    int    `json:"step,omitempty"`
	Message string `json:"message"`
}

func callAgentControl(request map[string]interface{}) (agentControlReply, error) {
	var reply agentControlReply
	socketPath := envOrDefault(
		"AITVBOX_AGENT_CONTROL_SOCKET",
		"/var/run/aitvbox/control.sock",
	)
	connection, err := net.DialTimeout("unix", socketPath, 2*time.Second)
	if err != nil {
		return reply, fmt.Errorf("computer control service is unavailable: %w", err)
	}
	defer connection.Close()
	_ = connection.SetDeadline(time.Now().Add(4 * time.Second))

	if err := json.NewEncoder(connection).Encode(request); err != nil {
		return reply, fmt.Errorf("cannot send computer control request: %w", err)
	}
	decoder := json.NewDecoder(io.LimitReader(connection, agentControlReplyMax))
	if err := decoder.Decode(&reply); err != nil {
		return reply, fmt.Errorf("invalid computer control response: %w", err)
	}
	if !reply.OK {
		if reply.Message == "" {
			reply.Message = "computer control request failed"
		}
		return reply, errors.New(reply.Message)
	}
	return reply, nil
}

func rpcGetAgentStatus() (agentControlReply, error) {
	return callAgentControl(map[string]interface{}{"command": "status"})
}

func rpcConfigureAgent(endpoint, model string) (agentControlReply, error) {
	return callAgentControl(map[string]interface{}{
		"command":  "configure",
		"endpoint": endpoint,
		"model":    model,
	})
}

func rpcSetAgentKey(key string) (agentControlReply, error) {
	return callAgentControl(map[string]interface{}{
		"command": "setKey",
		"key":     key,
	})
}

func rpcClearAgentKey() (agentControlReply, error) {
	return callAgentControl(map[string]interface{}{"command": "clearKey"})
}

func rpcStartAgent(task string, maxSteps float64) (agentControlReply, error) {
	if maxSteps < 1 || maxSteps > 30 || maxSteps != float64(int(maxSteps)) {
		return agentControlReply{}, errors.New("maxSteps must be an integer from 1 to 30")
	}
	if err := ensureHDMIPreviewForAgent(); err != nil {
		logger.Warn().Err(err).Msg("cannot prepare HDMI capture for computer control")
		return agentControlReply{}, err
	}
	reply, err := callAgentControl(map[string]interface{}{
		"command":  "start",
		"task":     task,
		"maxSteps": int(maxSteps),
	})
	if err != nil {
		logger.Warn().Err(err).Msg("cannot start computer control agent")
		stopHDMIPreviewForAgent()
	}
	return reply, err
}

func rpcStopAgent() (agentControlReply, error) {
	reply, err := callAgentControl(map[string]interface{}{"command": "stop"})
	stopHDMIPreviewForAgent()
	return reply, err
}

func ensureHDMIPreviewForAgent() error {
	agentCapture.Lock()
	if agentCapture.active {
		agentCapture.Unlock()
		return nil
	}
	// The agent is an additional consumer of the existing IPKVM capture
	// pipeline. platformStartVideo reference-counts browser and agent owners,
	// so neither side can stop /dev/video0 while the other still needs it.
	platformStartVideo()
	agentCapture.active = true
	agentCapture.Unlock()

	program := envOrDefault("AITVBOX_HDMI_PREVIEW", "/usr/bin/hdmi_preview")
	deadline := time.Now().Add(10 * time.Second)
	for time.Now().Before(deadline) {
		if captureSnapshotReady(program) {
			logger.Info().Msg("shared IPKVM capture produced an MCP snapshot")
			return nil
		}
		time.Sleep(100 * time.Millisecond)
	}
	stopHDMIPreviewForAgent()
	return errors.New("shared HDMI capture did not produce a snapshot")
}

func stopHDMIPreviewForAgent() {
	agentCapture.Lock()
	active := agentCapture.active
	agentCapture.active = false
	agentCapture.Unlock()
	if active {
		platformStopVideo()
	}
}

func captureSnapshotReady(program string) bool {
	ctx, cancel := context.WithTimeout(context.Background(), 7*time.Second)
	defer cancel()
	return exec.CommandContext(ctx, program, "--snapshot").Run() == nil
}

func unixSocketAcceptsConnections(path string) bool {
	connection, err := net.DialTimeout("unix", path, 100*time.Millisecond)
	if err != nil {
		return false
	}
	_ = connection.Close()
	return true
}
