# Distributed File Storage Platform

A C++ distributed file storage platform with chunk-based parallel transfers,
AES-256 encryption, SHA-256 integrity verification, Zstd compression, and
file versioning across Dockerized storage nodes.

**Stack:** C++20 · Boost.Asio · PostgreSQL · Redis · Docker · OpenSSL · Zstd

---

## Progress Log

### Stage 0 — Environment Setup 
- Installed Visual Studio Build Tools (MSVC 19.51.36256.0, x64, C++20)
- Installed CMake 4.4.2, added to system PATH
- Installed and bootstrapped vcpkg (`C:\vcpkg`), integrated system-wide
- Set up VS Code with C/C++ and CMake Tools extensions
- Verified: full configure → build → run cycle works end to end

### Stage 1 — Boost.Asio Networking Basics ✅
- Installed `boost-asio` + `boost-system` via vcpkg (Boost 1.91.0)
- Built a synchronous TCP echo server (`tcp::acceptor`, `tcp::socket`,
  blocking `accept()` / `read_some()` / `write()`)
- Verified with a Python socket client: connect → send → server echoes →
  client receives
- Later converted to an **async** version using `async_read_some` /
  `async_write` with callbacks and a `Session` class
  (`enable_shared_from_this` to keep sessions alive across pending async ops)

### Stage 2 — File Chunking + SHA-256 Integrity ✅
- Installed OpenSSL 3.6.3 via vcpkg
- Built `dfs::chunkFile`, `dfs::reassembleFile`, `dfs::sha256Hex` in
  `src/chunker.hpp` using OpenSSL's EVP API
- 1 MiB fixed chunk size; each chunk hashed and stored as `<hash>.chunk`
- Verified: ~1.9MB test file → 2 chunks → reassembled → byte-identical to
  original

**Toolchain note:** `find_package(OpenSSL REQUIRED)` and CMake's bundled
`FindBoost.cmake` module both triggered a parser error on this machine
(`FindPackageHandleStandardArgs.cmake` — expected "(", got "main"), traced
to that specific system CMake module file getting corrupted with unrelated
content. Workaround: use `find_package(Boost CONFIG REQUIRED COMPONENTS system)`
instead of module mode, and link OpenSSL directly via
`target_include_directories` / `target_link_directories` pointing at
vcpkg's `x64-windows/include` and `lib`, linking `libcrypto` rather than
using `find_package(OpenSSL)`. If a future dependency's `find_package(...)`
hits the same error, this is the likely cause.

### Stage 3 — Robust Chunk Metadata (in progress)
-### Stage 3 — Robust Chunk Metadata ✅
- Installed `nlohmann-json` via vcpkg (header-only, no linking needed)
- Added `ChunkMetadata` struct (fileId, fileName, chunkIndex, chunkSize,
  originalFileSize, sha256, version, createdAt) in new `src/metadata.hpp`
- Added UUID v4 `fileId` generation
- `chunkFileWithMetadata` produces full metadata records per chunk
- `saveMetadata` / `loadMetadata` persist/reload a file's chunk list as
  `metadata/<fileId>.json`
- `reassembleFromMetadata` rebuilds a file purely from loaded metadata
  (sorted by `chunkIndex`), independent of any in-memory chunk list from
  the original chunking run
- Verified: full round trip (chunk → save metadata → reload metadata →
  reassemble) produces a byte-identical file, simulating a fresh process
  that only knows the `fileId`

**Milestone:** metadata is now the durable source of truth for
reconstructing a file — this is the pattern Stage 10 (PostgreSQL) will
formalize into real tables.

---

### Stage 4 — Storage Node Abstraction ✅
- Added `dfs::StorageNode` class (`src/storage_node.hpp`) wrapping chunking,
  hashing, and metadata behind a clean interface: `store()`, `retrieve()`,
  `verify()`, `deleteFile()`, `listFiles()`
- Each node is rooted at a configurable `basePath` (chunks/ and metadata/
  live under it) — groundwork for running multiple independent nodes later
- `verify()` re-reads and re-hashes every chunk on disk, comparing against
  the SHA-256 recorded in metadata — real integrity checking, not just an
  existence check
- Verified full lifecycle: store → verify (OK) → retrieve → byte-match →
  list (1 file) → delete → list (0 files)

### Stage 5 — Boost.Asio Client/Server File Transfer Protocol ✅
- Designed a length-prefixed wire protocol (`src/protocol.hpp`):
  `[1B command][4B payload length][payload]`
- Commands: `CMD_STORE`, `CMD_RETRIEVE`, `CMD_LIST`, `CMD_DELETE`, `CMD_ERROR`
  (prefixed with `CMD_` after discovering `ERROR`/`DELETE` collide with
  Windows SDK macros pulled in transitively via Boost.Asio)
- Split into two executables: `dfs_server` and `dfs_client`
  (`dfs_main`/`main.cpp` retired)
- Server (`src/server.cpp`) uses async `accept` (Stage 1 pattern) +
  thread-per-connection, looping per session so one TCP connection can
  carry multiple requests (STORE then RETRIEVE, etc.) rather than closing
  after a single command
- Client (`src/client.cpp`) connects once, sends `STORE` then `RETRIEVE`
  over the same persistent connection
- Server wraps the Stage 4 `StorageNode` directly — network commands map
  1:1 onto `store()` / `retrieve()` / `listFiles()` / `deleteFile()`
- Verified: client uploads a file over real TCP, receives a `fileId`,
  downloads it back, and the bytes match the original exactly —
  full network round trip confirmed

**Debugging notes from this stage:**
- `ERROR`/`DELETE` as raw enum names caused cascading MSVC parse errors
  due to Windows macro collision — renamed with a `CMD_` prefix
- Initial version had the server closing the connection after one
  message while the client expected to reuse it for a second request —
  fixed by wrapping the server's per-client handler in a loop
- `LNK1168: cannot open ... for writing` during rebuild — caused by the
  previous `dfs_server.exe` still running; always stop the server
  (Ctrl+C) before rebuilding

**Milestone:** the project is no longer local-only — a client and server
process now exchange real files over TCP, backed by the full
chunk/hash/metadata pipeline from Stages 2–4.

### Stage 6 — Multiple Storage Nodes ✅
- `server.cpp` now takes port and data-directory as command-line args
  (`dfs_server.exe <port> <dataDir>`), so multiple independent instances
  can run as separate "nodes"
- Added `src/coordinator.hpp` (`dfs::Coordinator`) — routes files to nodes
  using round-robin placement, persists the file→node mapping in
  `placement.json` so retrievals know which node to ask
- New `dfs_coordinator_test` executable exercising the coordinator against
  3 real running node processes
- Verified: 3 nodes (ports 6001/6002/6003) running simultaneously, 4 test
  files distributed round-robin (file 4 correctly wrapped back to node 0),
  all retrieved and byte-matched correctly

**Known simplification:** placement currently happens at the *file* level
(a whole file goes to one node), not the *chunk* level (one file's chunks
spread across several nodes). True per-chunk distribution needs either
inter-node communication or per-chunk placement tracking — deferred to a
later stage once replication (Stage 7) is in place, since the two are
easier to design together.

**Milestone:** the system is now genuinely distributed — multiple
independent server processes, each unaware of the others, coordinated by
a client-side router that knows where everything lives.

### Stage 7 — Chunk-Level Distribution + Replication ✅
- Rewrote the coordinator to distribute individual **chunks** across nodes
  (not whole files) — true per-chunk distribution
- Each chunk is sent to a **primary** node and a **replica** node
  (replica = next node in the rotation), so no single node loss loses data
- New protocol commands: `CMD_STORE_CHUNK`, `CMD_RETRIEVE_CHUNK`
- `StorageNode` gained `storeChunk()` / `retrieveChunk()` for raw
  hash-addressed chunk storage
- Per-file manifests (`manifests/<fileId>.json`) record each chunk's index,
  hash, primary node, and replica node
- Retrieval logic: try the primary node; on connection failure,
  automatically fall back to the replica
- **Fault tolerance verified directly:** killed the port-6001 node process
  entirely mid-session, then retrieved a file whose chunks 0 and 3 had
  that node as primary — retrieval correctly fell back to the replica
  node for those chunks and reassembled a byte-perfect file
  (`fc` confirmed no differences)

**Milestone:** this is the first stage that proves resilience, not just
correctness — the system now survives losing a node.

### Stage 8 — AES-256-GCM Encryption ✅
- New `src/crypto.hpp` (`dfs::crypto`): `generateKey()`, `encrypt()`,
  `decrypt()` using OpenSSL's EVP API with AES-256-GCM (authenticated
  encryption — detects tampering, not just confidentiality)
- Each chunk gets a random 12-byte IV; stored format is
  `[IV][ciphertext][16-byte auth tag]`
- **Design decision:** hash the plaintext first (stable content identity,
  survives re-encryption), then encrypt — ciphertext is stored under the
  plaintext's hash
- One random AES-256 key generated per file, stored as hex in that file's
  manifest (`manifests/<fileId>.json`) — flagged as a simplification; a
  production system would use a proper key management service instead of
  storing keys alongside metadata
- `StorageNode::storeChunkAs(hash, data)` added — nodes now store opaque
  ciphertext under a hash the coordinator provides, since they never see
  plaintext and can't meaningfully self-hash anymore
- Protocol's `CMD_STORE_CHUNK` payload extended to carry the hash
  alongside the chunk bytes
- Verified: full encrypted distribute → replicate → retrieve → decrypt
  round trip byte-matched the original file on first build; confirmed by
  inspecting a `.chunk` file on disk — genuinely opaque binary, not
  readable plaintext

**Milestone:** data at rest on every node is now encrypted — even a full
compromise of a single node's disk reveals nothing without the per-file
key.
## How to Build

```powershell
cd C:\Users\patid\Desktop\distributed-system
Remove-Item -Recurse -Force build -ErrorAction SilentlyContinue
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build build --config Debug
```

## How to Run

```powershell
.\build\Debug\dfs_main.exe
```

## Project Structure

distributed-system/
├── CMakeLists.txt
├── README.md
├── .vscode/
│ └── settings.json
├── src/
│ ├── main.cpp
│ └── chunker.hpp
├── build/
│ └── Debug/
│ └── dfs_main.exe
├── test_input.txt
├── test_output.txt
└── chunks/

## Environment

| | |
|---|---|
| OS | Windows (native) |
| Compiler | MSVC 19.51.36256.0 |
| CMake | 4.4.2 |
| vcpkg | 2026-07-27, triplet `x64-windows` |
| Boost | 1.91.0 (asio, system) |
| OpenSSL | 3.6.3 |