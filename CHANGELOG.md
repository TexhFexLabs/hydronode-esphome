# Changelog

All notable changes to HydroNode ESPHome are documented here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and the project intends
to use [Semantic Versioning](https://semver.org/).

## [Unreleased]

## [0.2.0] - 2026-09-28

### Added

- Optional `commands` block that declares the commands a device handles, each
  with a value type (`BOOL`, `INT32`, `UINT32`, `INT64`, `UINT64`, `STRING`).
- `type` variable in `on_command` with the command's value type.

### Changed

- With declared commands, undeclared names, other value types and values that
  do not fit the type are declined with a reason (`NO_HANDLER`,
  `TYPE_MISMATCH`, `INVALID_VALUE`) instead of confirmed, and never reach
  `on_command`. The HydroNode apps show the reason.
- Without a `commands` block every command is confirmed and passed on, as in
  0.1.0.

## [0.1.0] - 2026-07-24

### Added

- ESPHome external component for signed HydroNode sensor uploads.
- Multiple ESPHome sensor mappings per HydroNode sensor.
- Existing HydroNode command delivery and signed acknowledgements.
- `hydronode.send` automation action.
- Upload success, upload error and command automation triggers.
- ESP-IDF and Arduino compile configurations.
- HMAC contract vectors, CI, examples and full documentation.
- Full CA certificate bundle for reliable HydroNode TLS verification across
  Cloudflare certificate-chain rotations.
- Tag-triggered release workflow gated on the validation matrix.

[Unreleased]: https://github.com/TexhFexLabs/hydronode-esphome/compare/v0.2.0...HEAD
[0.2.0]: https://github.com/TexhFexLabs/hydronode-esphome/compare/v0.1.0...v0.2.0
[0.1.0]: https://github.com/TexhFexLabs/hydronode-esphome/releases/tag/v0.1.0
