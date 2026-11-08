# mbo/digest - message digests

`mbo::digest` provides constexpr-compatible message digests, extendable-output functions, and
message authentication codes. Its APIs support one-shot and incremental computation, and its
command-line tool generates and verifies file checksums.

Scope: this library is cryptographic **message digests and MACs** (below). Fast
hash-table hashing (the `GetHash64` / `Hasher` API) lives in
[mbo/hash](../hash/README.md) - and so does **SipHash**, the keyed,
hash-flooding-resistant PRF: a keyed hash-table hash is not a message digest,
even though `Hmac` here is keyed too.

## What this library is

- **Spec-frozen algorithms, transcribed from their specifications** and pinned
  against independently generated reference vectors (FIPS/RFC examples plus
  padding-boundary and million-byte inputs). Implemented: SHA-224/256,
  SHA-384/512, SHA-512/224, SHA-512/256 (FIPS 180-4), SHA3-224/256/384/512
  (FIPS 202), BLAKE2b (RFC 7693), `Hmac<Algo>` over any of them (RFC 2104),
  BLAKE3 (XOF, keyed, and derive-key modes; pinned against the official
  test-vector suite), SHAKE128/256 (FIPS 202 XOFs), and SHA-1 + MD5 for
  legacy interop (both loudly marked collision-broken).
- **constexpr-safe**: every digest computable at compile time and at run time
  with identical results.
- **Apache-2.0, hermetic, verifiable**: original transcriptions with upstream
  attribution where due (see the repository-root [NOTICE](../../NOTICE)); reproducible builds;
  no vendored binaries, no live-at-head dependencies.

## Why not depend on a crypto library (BoringSSL et al.)

Digests are pure, spec-frozen functions - they cannot rot, and their
correctness is mechanically checkable against official vectors. A crypto
library dependency buys nothing here and costs a lot: BoringSSL is explicitly
"not intended for general use", unversioned, live-at-head, and has a history
of non-reproducible release archives - an unverifiable supply chain for
exactly the code that most needs verifying. We implement the functions
in-repo, verify them against the published vectors, and ship them under one
license with one [NOTICE](../../NOTICE) file.

## The `digest` binary

`bazel run //mbo/digest:digest -- [-a <algorithm>] <file>...` prints
checksum-style `<hash>  <file>` lines (the `sha256sum`/`shasum` format;
`--reverse` swaps the columns, `-d`/`--ignore_directories` silently skips
directories, `-` reads stdin). `-c`/`--check` verifies checksum files instead
(OK/FAILED per listed file, coreutils-interchangeable in both directions;
companions `--quiet`, `--status`, `--ignore_missing`, `--strict`). All
library algorithms are selectable; see `--help`.

## Honest guidance per algorithm

- **MD5**: ubiquitous legacy file checksum. Fine for detecting _accidental_
  corruption; **collision-broken against adversaries** (two colliding files
  cost seconds) - never use it where untrusted parties influence content.
- **SHA-1**: collision-broken (SHAttered); legacy interop only.
- **SHA-2 / SHA-3 / BLAKE**: current-generation; use SHA-256 for adversarial
  integrity, BLAKE3/XXH128 (the latter in [mbo/hash](../hash/README.md)) for fast file identity.

## What this library will never be

AEAD ciphers, signatures, key exchange, RNGs, TLS - actual cryptography with
keys and side channels - are **permanent non-goals**. That work needs
maintained implementations and constant-time expertise; a constexpr AES would
be a footgun. This library is deliberately named _digest_, not _crypto_.
