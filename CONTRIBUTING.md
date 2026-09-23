# Contributing

Contributions are welcome after signing [CLA.md](CLA.md). By submitting a
change, contributors agree that the contribution may be distributed under the
project's GPL and commercial licensing model.

Required workflow:

1. Keep domain/application code independent from vendor SDKs and device nodes.
2. Put hardware differences in `platforms/<id>` behind `core/ports`.
3. Add or update contract, unit and integration tests with every behavior
   change.
4. Run `python3 tools/aitvbox.py test --platform a133 --scope architecture`.
5. For A133 runtime changes, run the host suite and all affected cross-builds.
6. Do not commit build outputs, SDK files, credentials or factory data.

New hardware support is accepted only with its manifest, provider, build and
packaging profile, porting notes, and a completed physical validation report.
