# Changelog

All notable changes to HydroNode ESPHome are documented here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and the project intends
to use [Semantic Versioning](https://semver.org/).

## [Unreleased]

## [0.4.0] - 2026-10-02

### Added

- Every request carries `X-Firmware: esphome-hydronode/0.4.0 <chip>` and
  `X-Device-Status: boot=<n>;reset=<reason>;uptime=<s>;rssi=<dBm>;net=wifi`
  for the HydroNode fleet view. The boot counter lives in the ESPHome
  preferences and counts cold starts, not wake-ups from deep sleep. Nothing to
  configure; the component takes no updates over the air through HydroNode.

## [0.3.0] - 2026-10-02

### Added

- A command name may be declared once per value type, for example `relay1` as
  `BOOL` to switch and as `UINT32` to switch on for a number of milliseconds.
  A typed command needs its type declared; an untyped command (older app
  versions) takes the first declared type its value fits. Matches
  HydroNode-Library 1.5.0.

## [0.2.2] - 2026-09-29

### Changed

- Default `base_url` is now `https://hydronode.tech`. The previous host
  `https://hydronode.texhfexlabs.de` keeps accepting device uploads, so
  configurations that set it explicitly continue to work without changes.

## [0.2.1] - 2026-09-28

### Fixed

- Increase the default command-response buffer from 2048 to 16384 bytes so a
  full delivery of eight long commands, including UTF-8 text, is not truncated
  and silently left unacknowledged. Existing explicit smaller buffer settings
  must be removed or increased to use the new capacity.
- Preserve the full integer part of large finite measurements when formatting
  the signed two-decimal payload, instead of truncating the value in a fixed
  32-byte buffer.

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

[Unreleased]: https://github.com/TexhFexLabs/hydronode-esphome/compare/v0.3.0...HEAD
[0.3.0]: https://github.com/TexhFexLabs/hydronode-esphome/compare/v0.2.2...v0.3.0
[0.2.2]: https://github.com/TexhFexLabs/hydronode-esphome/compare/v0.2.1...v0.2.2
[0.2.1]: https://github.com/TexhFexLabs/hydronode-esphome/compare/v0.2.0...v0.2.1
[0.2.0]: https://github.com/TexhFexLabs/hydronode-esphome/compare/v0.1.0...v0.2.0
[0.1.0]: https://github.com/TexhFexLabs/hydronode-esphome/releases/tag/v0.1.0
