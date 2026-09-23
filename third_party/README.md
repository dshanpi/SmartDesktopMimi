# Third-Party Dependencies

`TuyaOpen` is intentionally not treated as product-owned source.

`third_party/TuyaOpen` is a vendored source tree based on the revision recorded
in `components.json`. Product-owned build integration and the archived patch
series live under `integrations/tuyaopen/`. Restricted A133_B6 libraries live
under `vendor/` and are never exported.

The development repository has one source submodule, the product LVGL fork:

```sh
git submodule update --init --recursive
```

The history-free public exporter materializes that LVGL revision and applies
`docs/public-release.json`. It removes non-A133 Tuya/LVGL examples, test
corpora, downloaded images and unclassified prebuilt libraries. Do not treat a
path under `third_party/` as evidence that its contents are redistributable.
