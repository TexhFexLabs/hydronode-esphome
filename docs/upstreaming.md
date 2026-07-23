# Upstreaming to official ESPHome

HydroNode ESPHome can be used immediately as an external component. ESPHome does
not have an external-component registry that must approve this repository.
Official inclusion means moving and maintaining the integration in the upstream
ESPHome projects.

This checklist reflects the ESPHome contribution documentation available when
the repository was created. Re-read the linked official documentation before
submitting because its requirements evolve.

## 1. Prove the external component

- Publish the repository under the `TexhFexLabs` organization.
- Enable GitHub Actions and branch protection.
- Create a signed or annotated `v0.1.0` tag and GitHub release after the first
  public review.
- Test several real ESP32 boards, both frameworks and several sensor platforms.
- Collect issue reports and establish an active maintainer.
- Keep the HydroNode API backward-compatible and publicly documented.

This phase is valuable even if the integration always remains external.

## 2. Ask whether it is an upstream fit

Read ESPHome's current
[component architecture](https://developers.esphome.io/architecture/components/)
and [contribution guide](https://developers.esphome.io/contributing/code/).
Discuss a vendor-cloud component with the ESPHome maintainers before investing
in a large migration. They decide whether the integration belongs in core or
should remain external.

Be ready to explain:

- the number of existing users and devices;
- why a native component is better than YAML-only `http_request` calls;
- the security and maintenance model;
- ownership of the HydroNode service and API;
- behavior if the cloud is unreachable or discontinued;
- why no automatic provisioning or backend change is needed.

Maintainers can decline or request architectural changes. Remaining a
well-maintained external component is a normal outcome, not a technical
limitation.

## 3. Prepare the ESPHome code pull request

Follow the current
[submitting your work](https://developers.esphome.io/contributing/submitting-your-work/)
guide. At minimum:

- fork `esphome/esphome` and branch from its current development branch;
- move the component into `esphome/components/hydronode`;
- follow upstream naming, schema, code-generation, lint and style conventions;
- add the required `CODEOWNERS` metadata;
- add upstream test configurations covering successful schemas and validation
  failures;
- compile all required platforms/frameworks;
- run the repository's formatting, linting and test commands;
- keep the pull request focused and describe the wire protocol and security
  implications;
- sign the contribution agreement if requested;
- respond to review and squash/fix commits according to maintainer guidance.

The external repository's code is a starting point, not a promise that it can
be copied unchanged. Upstream internal APIs and quality requirements may differ.

## 4. Prepare the separate documentation pull request

Follow ESPHome's current
[documentation contribution guide](https://developers.esphome.io/contributing/docs/)
and open a separate pull request against
[`esphome/esphome.io`](https://github.com/esphome/esphome.io).

The documentation should:

- be written in English;
- include a minimal, valid YAML example;
- document every option, action, trigger and variable;
- link to the HydroNode service and its privacy/security information;
- state that a HydroNode account/device credential is required;
- describe rate limits, synchronous HTTP behavior and cloud dependency;
- avoid presenting provisioning as automatic when it is manual;
- follow the current Astro/MDX structure and formatting checks.

Coordinate the code and documentation pull requests by linking them to each
other.

## 5. Maintain the integration

Official inclusion creates an ongoing obligation:

- review HydroNode issues and pull requests;
- track ESPHome API deprecations and framework updates;
- preserve the existing configuration contract;
- publish migration notes for breaking changes;
- coordinate responsibly disclosed security fixes;
- keep the service endpoints and authentication behavior stable.

## Backend impact

None of the steps above inherently requires a HydroNode backend change. The
current component already uses the deployed signed value and command-ACK
endpoints. Features such as automatic provisioning, batched ingestion or
timestamp-preserving offline backfill would be separate product/API projects and
are intentionally outside this upstreaming effort.
