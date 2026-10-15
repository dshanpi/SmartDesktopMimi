SDK_ROOT ?= /home/ubuntu/A133-Tina5.0-v0.9
GO ?= /usr/local/go/bin/go
NPM ?= npm
NPM_REGISTRY ?= https://registry.npmjs.org
BUILD_DIR ?= $(CURDIR)/build
VERSION ?= 0.1.0
UPSTREAM_DIR := $(CURDIR)/upstream
CAPTURE_BUILDER := $(abspath $(CURDIR)/../../scripts/build_a133_capture.py)

.PHONY: all check-capture frontend web video test clean

all: check-capture
	$(MAKE) web video

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

check-capture:
	python3 "$(CAPTURE_BUILDER)" --sdk "$(abspath $(SDK_ROOT))" --check

video: check-capture
	mkdir -p "$(BUILD_DIR)"
	@run="$$(mktemp -d "$(BUILD_DIR)/capture.XXXXXX")" && \
		python3 "$(CAPTURE_BUILDER)" --sdk "$(abspath $(SDK_ROOT))" --output "$$run/compiled" && \
		cp -f "$$run/compiled/aitvbox-kvm-video" "$(BUILD_DIR)/aitvbox-kvm-video" && \
		cp -f "$$run/compiled/build-result.json" "$(BUILD_DIR)/capture-build-result.json"

test:
	cd $(UPSTREAM_DIR) && $(GO) test ./...

clean:
	rm -rf $(BUILD_DIR)
