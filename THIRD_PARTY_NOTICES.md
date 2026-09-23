# Third-party notices

This inventory is a release gate, not a substitute for the license files
shipped by each component.

| Component | Location | License/boundary |
| --- | --- | --- |
| LVGL | `apps/lv_port_linux/lvgl` | MIT, pinned fork; public export omits upstream demos/tests and unrelated prebuilt MCU libraries |
| IPKVM upstream | `apps/ipkvm/upstream` | GPL-2.0-only, separate process/package; downloaded netboot ISO, legacy ARM executable and unlicensed CircularXX webfonts are excluded |
| TuyaOpen | `third_party/TuyaOpen` | Apache-2.0 and component licenses; public export contains the A133 build subset only |
| Sarasa Gothic | `apps/lv_port_linux/src/ui/font/*.ttf` | OFL-1.1; hashes and source are recorded in `docs/public-release.json` |
| json-c | target system package | MIT |
| libcurl | target system package | curl license |
| SWUpdate | target system integration | GPL-2.0 and component terms |
| Vendor media/KWS libraries | `vendor/` | Restricted external build inputs; never included in a public source snapshot |

The 100ask IoT SDK is an optional external provider used only by explicitly
configured internal builds. It is not included in this source distribution or
its SBOM; users must obtain it and review its license separately.

Generate SPDX and CycloneDX SBOM files from the exact release snapshot and
firmware package list. Unresolved or restricted entries block public release.
The public export policy and every retained binary asset are recorded in
`docs/public-release.json`; a third-party directory name is not itself a
redistribution approval.
