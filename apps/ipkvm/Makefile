SDK_ROOT ?= /home/ubuntu/A133-Tina5.0-v0.9
GO ?= /usr/local/go/bin/go
NPM ?= npm
NPM_REGISTRY ?= https://registry.npmjs.org
BUILD_DIR ?= $(CURDIR)/build
VERSION ?= 0.1.0
UPSTREAM_DIR := $(CURDIR)/upstream
VIDEO_DIR := $(CURDIR)/video

.PHONY: all frontend web video test clean

all: web video

frontend:
	cd $(UPSTREAM_DIR)/ui && \
		$(NPM) ci --no-audit --no-fund --registry=$(NPM_REGISTRY) && \
		$(NPM) run build:device

web: frontend
	mkdir -p $(BUILD_DIR)
	cd $(UPSTREAM_DIR) && \
		CGO_ENABLED=0 GOOS=linux GOARCH=arm64 \
		$(GO) build -trimpath -tags netgo \
		-ldflags="-s -w -X kvm.builtAppVersion=$(VERSION)" \
		-o $(BUILD_DIR)/aitvbox-ipkvmd ./cmd

video:
	$(MAKE) -C $(VIDEO_DIR) SDK_ROOT=$(SDK_ROOT) \
		BUILD_DIR=$(BUILD_DIR) all

test:
	cd $(UPSTREAM_DIR) && $(GO) test ./...

clean:
	rm -rf $(BUILD_DIR)
	$(MAKE) -C $(VIDEO_DIR) clean
