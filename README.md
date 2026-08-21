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

**Milestone:** `main.cpp` no longer touches chunking/hashing/metadata
internals directly — it only calls methods on `StorageNode`. This is the
object Stage 5 will expose over the network.

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