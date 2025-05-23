# Trezor Firmware documentation

_This documentation can also be found at [docs.trezor.io](https://docs.trezor.io) where it is available in a HTML-built version compiled using [mdBook](https://github.com/rust-lang/mdBook)._

Welcome to the Trezor Firmware repository. This repository is so called _monorepo_, it contains several different yet very related projects that together form the Trezor Firmware ecosystem.

## Repository Structure

* **[`ci`](https://github.com/trezor/trezor-firmware/tree/main/ci/)**: Helper files, data, and scripts for the CI pipeline
* **[`common/defs`](https://github.com/trezor/trezor-firmware/tree/main/common/defs/)**: JSON coin definitions and support tables
* **[`common/protob`](https://github.com/trezor/trezor-firmware/tree/main/common/protob/)**: Common protobuf definitions for the Trezor protocol
* **[`common/tools`](https://github.com/trezor/trezor-firmware/tree/main/common/tools/)**: Tools for managing coin definitions and related data
* **[`core`](https://github.com/trezor/trezor-firmware/tree/main/core/)**: Trezor Core, firmware implementation for Trezor T
* **[`crypto`](https://github.com/trezor/trezor-firmware/tree/main/crypto/)**: Stand-alone cryptography library used by both Trezor Core and the Trezor One firmware
* **[`docs`](https://github.com/trezor/trezor-firmware/tree/main/docs/)**: Assorted documentation
* **[`legacy`](https://github.com/trezor/trezor-firmware/tree/main/legacy/)**: Trezor One firmware implementation
* **[`python`](https://github.com/trezor/trezor-firmware/tree/main/python/)**: Python [client library](https://pypi.org/project/trezor) and the `trezorctl` command
* **[`storage`](https://github.com/trezor/trezor-firmware/tree/main/storage/)**: NORCOW storage implementation used by both Trezor Core and the Trezor One firmware
* **[`tests`](https://github.com/trezor/trezor-firmware/tree/main/tests/)**: Firmware unit test suite
* **[`tools`](https://github.com/trezor/trezor-firmware/tree/main/tools/)**: Miscellaneous build and helper scripts
* **[`vendor`](https://github.com/trezor/trezor-firmware/tree/main/vendor/)**: Submodules for external dependencies


## Contribute

See [CONTRIBUTING.md](https://github.com/trezor/trezor-firmware/tree/main/CONTRIBUTING.md).

Also please have a look at the docs, either in the `docs` folder or at  [docs.trezor.io](https://docs.trezor.io) before contributing. The [misc](misc/index.md) chapter should be read in particular because it contains some useful assorted knowledge.

## Security vulnerability disclosure

Please report suspected security vulnerabilities in private to [security@satoshilabs.com](mailto:security@satoshilabs.com), also see [the disclosure section on the Trezor.io website](https://trezor.io/security/). Please do NOT create publicly viewable issues for suspected security vulnerabilities.

## Note on terminology

The key words "MUST", "MUST NOT", "REQUIRED", "SHALL", "SHALL NOT", "SHOULD",
"SHOULD NOT", "RECOMMENDED", "MAY", and "OPTIONAL" in this document are to be interpreted as described in [RFC 2119](https://tools.ietf.org/html/rfc2119).
