# Simulated Flash Contract

## Purpose

The flash simulator is the persistence boundary for the updater and boot-selection logic. Higher layers must use this API instead of reading or writing the backing file directly. The simulator deliberately exposes failure behavior that an ordinary host file does not.

The simulator knows nothing about firmware slots, image headers, metadata, confirmation, or rollback.

## Geometry

Every flash instance has three fixed geometry values:

- `total_size`: total addressable bytes.
- `sector_size`: alignment and length of an erase operation.
- `program_unit`: required address and length alignment for program operations.

The total size must be a multiple of the sector size, and the sector size must be a multiple of the program unit. The caller supplies the same geometry when formatting and reopening a backing file. A file with the wrong length is rejected.

## Operations

### Format

`flash_format` creates or replaces a backing file containing exactly `total_size` erased bytes (`0xFF`). Formatting represents initial provisioning and is outside the injected update fault model.

### Read

Reads copy bytes without changing persistent state. A read may end exactly at the device boundary but may not cross it. Reads are reliable in the MVP fault model.

### Program

A program request must:

- Have a nonzero length.
- Begin on a `program_unit` boundary.
- Have a length divisible by `program_unit`.
- Fit completely inside the device.
- Change every affected bit only from `1` to `0`.

The simulator validates the entire request before changing any bytes. This makes an illegal caller request a software error rather than an injected torn write. A valid program is then persisted one modeled byte position at a time.

Programming an already-zero bit as zero is legal. Programming a zero bit back to one is rejected; an erase is required first.

### Erase sector

An erase address must be sector-aligned, and the complete sector must fit inside the device. A completed erase sets the entire sector to `0xFF`. Neighboring sectors are not modified.

## Power-failure injection

`flash_inject_power_failure_after(flash, N)` arms a one-shot failure budget across subsequent persistent operations:

- `N = 0` fails before the first persistent byte position.
- `N > 0` allows exactly `N` byte positions to be written or erased and then reports power loss.
- If the final byte of an operation consumes the budget, all requested bytes may be durable even though the caller never receives success.

Every processed byte position consumes one unit of the budget, even if its value already equals the requested value. This keeps interruption offsets determined by operation position rather than previous contents.

The complete affected prefix is synchronized to the host file before a simulated fault is reported. A normally completed erase or program is also synchronized before success is returned. The simulator therefore does not rely on buffered host writes to represent the durable state observed after reconstruction, while avoiding an expensive host sync after every individual modeled byte.

After power loss, the flash instance is permanently powered off:

- Reads fail.
- Programs fail.
- Erases fail.
- Fault configuration changes fail.

The caller must discard the instance and reopen the backing file. This prevents the interrupted execution from performing cleanup writes. Later system-level tests will additionally terminate and reconstruct the entire process.

## Partial-operation pattern

The deterministic MVP model persists an address-ordered prefix of a program or erase request. This supports exhaustive byte-offset enumeration with small images.

This is not a claim that real partially erased NOR flash always contains a prefix of erased bytes. Non-prefix damage, bit-level analog behavior, stuck bits, wear, and retention faults are outside this model and can be added as separate corruption modes later.

## Why failure after the final byte matters

An operation may be physically complete even when the caller never observes a successful return. Higher-level protocols must never rely only on the interrupted caller's RAM knowledge. On reboot, they must inspect commit markers, CRCs, hashes, and other persisted evidence to determine whether the old or new state became authoritative.
