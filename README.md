# NEVO

Low-latency encrypted VoIP application — C++20 client/server architecture.

## Features

- **Low-latency audio**: miniaudio capture/playback + Opus codec with in-band FEC
- **Encrypted voice**: XChaCha20-Poly1305 AEAD with automatic key rotation
- **NAT traversal**: STUN binding + TCP fallback voice tunnel
- **Channel system**: hierarchical channel tree with permissions
- **Qt 6 UI**: dockable channel/user panels, connection bar, audio settings
- **Server dashboard**: real-time session monitoring, user management
- **Cross-platform**: Windows / Linux / macOS

## Tech Stack

| Component | Technology |
|-----------|-----------|
| Language | C++20 |
| Build | CMake 3.21+ |
| UI | Qt 6 (Widgets) |
| Async I/O | Boost.Asio (coroutines) |
| Audio | miniaudio + Opus |
| Encryption | libsodium (XChaCha20-Poly1305) |
| Protocol | Protobuf (hand-written generated headers) |
| Storage | SQLite3 |
| Logging | spdlog (embedded stub with fmt-style formatting) |

## Directory Structure

```
NEVO/
├── cmake/                  # CMake helper modules
├── proto/                  # Protobuf schema definitions
│   └── generated/          # Hand-written C++ protobuf replacements
├── 3rdparty/               # Embedded dependencies (miniaudio, spdlog stub)
├── src/
│   ├── core/               # Shared core library
│   │   ├── audio/          # AudioEngine, Opus, JitterBuffer, Mixer, VAD
│   │   ├── common/         # Result<T>, Logger, Types
│   │   ├── protocol/       # PacketCodec, frame encoding
│   │   └── permission/     # Permission system
│   ├── network/            # Network library
│   │   ├── TcpConnection   # Async TCP with frame protocol
│   │   ├── UdpSocket       # Async UDP
│   │   ├── VoiceCrypto     # Encryption/decryption
│   │   ├── NatTraversal    # STUN client
│   │   └── SslWrapper      # TLS support
│   ├── client/             # Client application
│   │   ├── ClientCore      # Client lifecycle & state machine
│   │   ├── NetworkManager  # Client network orchestration
│   │   └── AudioInput/Output
│   ├── server/             # Server application
│   │   ├── ServerCore      # Server lifecycle
│   │   ├── ClientSession   # Per-connection handler
│   │   ├── Database        # SQLite persistence
│   │   └── ui/             # Server dashboard (Qt)
│   └── ui/                 # Client UI (Qt 6)
│       ├── MainWindow      # Main window with dockable panels
│       ├── ChannelTreeModel
│       ├── UserListModel
│       └── ConnectionBar
└── tests/                  # Unit & integration tests (GTest)
```

## Building

### Prerequisites

| Dependency | Required | Notes |
|-----------|----------|-------|
| CMake 3.21+ | Yes | |
| C++20 compiler | Yes | MSVC 2022, GCC 12+, Clang 15+ |
| Qt 6 | Yes | Core, Widgets modules |
| Boost 1.80+ | Optional | system, lockfree, endian, context |
| OpenSSL | Optional | TLS support |
| Opus | Optional | Audio codec (stub without) |
| libsodium | Optional | Voice encryption (stub without) |
| SQLite3 | Optional | Server persistence (stub without) |

### Build Commands

```bash
# Configure
cmake -B build -DCMAKE_BUILD_TYPE=Release

# Build
cmake --build build --parallel

# Build with tests
cmake -B build -DBUILD_TESTING=ON
cmake --build build --parallel
ctest --test-dir build
```

### Conditional Compilation

All optional dependencies degrade gracefully via compile-time flags:

| Flag | Effect when OFF |
|------|----------------|
| `NEVO_HAS_BOOST` | No network/client modules (coroutine stubs) |
| `NEVO_HAS_OPUS` | Opus encode/decode returns silence/stub |
| `NEVO_HAS_SODIUM` | No voice encryption (plaintext) |
| `NEVO_HAS_SQLITE` | No server database (in-memory only) |
| `NEVO_HAS_OPENSSL` | No TLS (plaintext TCP) |

## Running

### Server

```bash
./nevo_server [--tcp-port 24430] [--udp-port 24431] [--db server.db] \
              [--threads 4] [--log-level info]
```

### Client

Launch the GUI application. Use the Connect dialog to enter server address, port, and credentials.

## Configuration

### Server Config File (JSON)

Create `server_config.json`:

```json
{
    "tcp_port": 24430,
    "udp_port": 24431,
    "db_path": "server.db",
    "threads": 4,
    "log_level": "info",
    "server_name": "NEVO Server",
    "max_users": 100,
    "welcome_message": "Welcome to the NEVO server!"
}
```

Priority: CLI arguments > config file > defaults.

### Client Settings

Client settings are automatically persisted via QSettings:
- Last server address/port
- Username
- Audio input/output volume
- VAD/PTT mode selection

## Docker

```bash
# Build image
docker build -t nevo-server .

# Run
docker-compose up -d
```

## Testing

```bash
cmake -B build -DBUILD_TESTING=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

Test coverage includes: Result<T>, Channel, Permission, Opus codec, JitterBuffer, AudioMixer, AudioMemoryPool, TcpConnection, VoiceCrypto, NatTraversal, Server integration.

## License

All rights reserved.
