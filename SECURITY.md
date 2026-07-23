# Security Policy

## Reporting a vulnerability

Please report suspected vulnerabilities privately to **contact@knollfelix.de**.
Do not open a public GitHub issue and do not include real HydroNode credentials.

Include the affected version, impact, reproduction steps and any proposed
mitigation. You should receive an acknowledgement within seven days. A fix and
coordinated disclosure timeline will be agreed after triage.

## Credential handling

Store the HydroNode sensor UUID and device secret in ESPHome `secrets.yaml`.
Never commit secrets, firmware binaries or build directories. If a secret or
firmware image is exposed, rotate the device secret in HydroNode and reflash the
device.
