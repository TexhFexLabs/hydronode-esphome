# Changelog

All notable changes to HydroNode ESPHome are documented here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and the project intends
to use [Semantic Versioning](https://semver.org/).

## [Unreleased]

### Changed

- Enable ESPHome's full CA certificate bundle for reliable HydroNode TLS
  verification across Cloudflare certificate-chain rotations.
- Clarify that mapped sensors can remain available in Home Assistant through
  ESPHome's native API.

## [0.1.0] - 2026-07-23

### Added

- ESPHome external component for signed HydroNode sensor uploads.
- Multiple ESPHome sensor mappings per HydroNode sensor.
- Existing HydroNode command delivery and signed acknowledgements.
- `hydronode.send` automation action.
- Upload success, upload error and command automation triggers.
- ESP-IDF and Arduino compile configurations.
- HMAC contract vectors, CI, examples and full documentation.

[Unreleased]: https://github.com/TexhFexLabs/hydronode-esphome/compare/v0.1.0...HEAD
[0.1.0]: https://github.com/TexhFexLabs/hydronode-esphome/releases/tag/v0.1.0
