# Firmware Image Format and Slice 2 Boot Policy

## Scope

Slice 2 proves that one provisioned confirmed slot is selected only when its complete image is structurally valid, committed, in bounds, and SHA-256-valid. Persistent boot metadata, pending images, retries, A/B update installation, and rollback are intentionally deferred.

For this slice, the caller supplies the confirmed slot identifier. Later, redundant committed metadata will supply that identifier.

## Why the header is serialized manually

The on-flash header is a byte protocol, not an in-memory C structure. Writing a C `struct` directly would make compiler padding, host endianness, alignment, and ABI choices part of the persistent format. The implementation instead loads and stores fixed-width little-endian fields at explicit offsets.

## Header layout

The header occupies 64 bytes. The payload begins immediately afterward.

| Offset | Size | Field | Validation |
|---:|---:|---|---|
| 0 | 4 | Magic (`PLS1`) | Exact value |
| 4 | 4 | Format version | Must equal supported version 1 |
| 8 | 8 | Firmware version | Reported to later policy layers |
| 16 | 4 | Payload size | Nonzero and contained within the slot |
| 20 | 32 | Payload SHA-256 | Must match the complete declared payload |
| 52 | 4 | Flags | Must be zero in format version 1 |
| 56 | 4 | Header CRC-32 | Covers bytes 0–55 |
| 60 | 4 | Commit marker (`CMIT`) | Exact value; written last |

All integer fields use little-endian encoding. The commit marker is excluded from the header CRC so it can remain erased while the rest of the header is constructed and then be programmed in a separate final operation.

The MVP flash geometry uses a four-byte program unit, matching the four-byte final commit field. Supporting devices with a larger minimum program unit would require reserving a correspondingly larger independently programmable commit region in a later format version.

## Validation order

1. Prove that the complete slot and fixed-size header lie inside flash.
2. Read the 64-byte header.
3. Require the exact commit marker.
4. Validate magic, format version, and flags.
5. Validate the header CRC.
6. Validate the payload size before any payload read.
7. Hash exactly the declared payload bytes through the flash API.
8. Compare the computed SHA-256 with the stored expected digest.

An invalid image produces a safe refusal. The boot selector never tries to repair it, infer missing fields, or boot a plausible-looking payload.

## CRC versus SHA-256

CRC-32 protects the small header against accidental corruption and will later be reused for metadata records. SHA-256 protects the complete payload against accidental corruption with a standard full-content digest.

Neither mechanism authenticates the publisher. An attacker could replace a payload and recompute both values. Signed manifests and secure boot remain outside the MVP claim.

## Commit behavior

The header builder leaves bytes 60–63 erased. `image_publish_commit_marker` programs the exact marker as a separate operation. The image validator rejects an erased, partial, or incorrect marker.

Publishing the marker alone is not enough to boot an image: all structural, CRC, bounds, and full-hash checks still have to pass.

## Current boot policy

`boot_select_confirmed` receives slot A or B as a trusted temporary input for Slice 2. It validates that slot and either:

- Returns it as the selected image; or
- Returns no selected slot and a diagnostic rejection reason.

Slice 4 will replace this temporary trusted selection input with the highest-generation independently valid committed metadata record.
