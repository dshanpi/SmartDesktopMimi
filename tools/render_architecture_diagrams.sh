#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
diagram_root="${repo_root}/docs/architecture-diagrams"
source_dir="${diagram_root}/sources"
rendered_dir="${diagram_root}/rendered"
config_file="${diagram_root}/mermaid-config.json"
mermaid_cli_version="11.12.0"
export PUPPETEER_SKIP_DOWNLOAD=true

if [[ -z "${PUPPETEER_EXECUTABLE_PATH:-}" ]]; then
    browser_cache_root="${XDG_CACHE_HOME:-${HOME}/.cache}/ms-playwright"
    for browser in \
        "$(command -v chromium 2>/dev/null || true)" \
        "$(command -v chromium-browser 2>/dev/null || true)" \
        "$(command -v google-chrome 2>/dev/null || true)" \
        "${browser_cache_root}/chromium-1200/chrome-linux64/chrome" \
        "${browser_cache_root}/chromium-1140/chrome-linux/chrome"; do
        if [[ -n "${browser}" && -x "${browser}" ]]; then
            export PUPPETEER_EXECUTABLE_PATH="${browser}"
            break
        fi
    done
fi

if [[ -z "${PUPPETEER_EXECUTABLE_PATH:-}" ]]; then
    echo "error: set PUPPETEER_EXECUTABLE_PATH to a Chromium/Chrome executable" >&2
    exit 1
fi

mkdir -p "${rendered_dir}"

render_one() {
    local source_file="$1"
    local stem
    stem="$(basename "${source_file}" .mmd)"

    npx --yes "@mermaid-js/mermaid-cli@${mermaid_cli_version}" \
        --input "${source_file}" \
        --output "${rendered_dir}/${stem}.svg" \
        --configFile "${config_file}" \
        --backgroundColor transparent \
        --quiet

    npx --yes "@mermaid-js/mermaid-cli@${mermaid_cli_version}" \
        --input "${source_file}" \
        --output "${rendered_dir}/${stem}.png" \
        --configFile "${config_file}" \
        --backgroundColor white \
        --width 2400 \
        --scale 1 \
        --quiet
}

for source_file in "${source_dir}"/*.mmd; do
    render_one "${source_file}"
done

echo "rendered architecture diagrams into ${rendered_dir}"
