# Contributing

Thanks for improving HydroNode ESPHome. Bug reports, documentation fixes, new
examples and focused code changes are welcome.

## Before opening an issue

- Search existing issues.
- Validate the YAML with the supported ESPHome version.
- Enable debug logging and remove WiFi credentials, API keys, sensor UUIDs and
  device secrets before sharing output.
- For hardware problems, include the exact board, framework, sensor model,
  wiring and a minimal configuration.

Security vulnerabilities must not be filed publicly. Follow
[SECURITY.md](SECURITY.md) instead.

## Development setup

```bash
git clone https://github.com/TexhFexLabs/hydronode-esphome.git
cd hydronode-esphome
python3 -m venv .venv
source .venv/bin/activate
pip install "esphome==2026.7.1"
```

Run the same checks as CI:

```bash
python -m unittest discover -s tests -v
esphome config tests/configs/minimal-idf.yaml
esphome config tests/configs/minimal-arduino.yaml
esphome compile tests/configs/minimal-idf.yaml
esphome compile tests/configs/minimal-arduino.yaml
```

## Pull requests

1. Keep changes focused and explain the user-visible behavior.
2. Add or update an example when adding YAML options or actions.
3. Add a contract vector or compile configuration when changing protocol or
   framework behavior.
4. Update `README.md` and `CHANGELOG.md`.
5. Confirm that both frameworks compile.
6. Use clear, imperative commit messages.

Do not add automatic provisioning or change the HydroNode API contract in this
repository without a separately reviewed backend design. Backward compatibility
with existing deployed credentials and endpoints is a core project requirement.

By submitting a contribution, you agree that it is licensed under the MIT
License in this repository.
