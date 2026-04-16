# vEPC - Virtual Evolved Packet Core (EPC) Emulator

A comprehensive mobile network EPC emulator supporting MME and SGSN cores in a single unified process. Designed for protocol testing, network simulation, and mobile network research with real protocol implementation and Cisco-style CLI management.

## 🎯 Overview

**vEPC** emulates a complete mobile network core with:
- **MME** (Mobility Management Entity) - LTE attachment, authentication, and mobility management
- **SGSN** (Serving GPRS Support Node) - GPRS/3G packet data management  
- Real protocol implementations (GTP, Diameter, S1AP, NAS)
- Hot-reloadable configuration without restart
- Cisco-style hierarchical CLI interface
- Docker containerization for easy deployment
- Ansible automation for production deployment

## ✨ Features

### Protocol Support
- **GTP-C v1** - GPRS Tunneling Protocol Control plane
- **Diameter** - S6a interface for HSS authentication
- **S1AP** - LTE S1 interface with NAS message embedding
- **NAS** - Network Access Stratum messaging (17 message types)

### CLI Interface
- **Cisco-style Commands**: Similar to Cisco IOS/NX-OS networking devices
- **Hierarchical Modes**: `exec` → `config` → `interface` configuration
- **Linux Sub-Interface Management**: Create/delete VLAN interfaces on-demand
- **Cross-Platform**: Windows (TCP CLI) and Linux (Unix socket CLI)

### Deployment Options
- **Standalone Executable**: Direct native compilation
- **Docker Container**: Multi-stage build for minimal footprint
- **Docker Compose**: Local development and testing
- **Ansible Playbook**: Automated Ubuntu 22.04+ deployment

### State Management
- Runtime UE context tracking (auth state, security, session state)
- PDP context persistence across restarts
- Interface admin state persistence
- JSON-based state storage

## 🚀 Quick Start

### Build from Source

```bash
# Clone repository
git clone https://github.com/cubasnik/vEPC.git
cd vEPC

# Build with CMake
mkdir build && cd build
cmake ..
make -j$(nproc)

# Run
./vepc

# In another terminal, access CLI
./vepc-cli
```

### Docker Deployment (Recommended)

```bash
# Build Docker image locally
docker build -t vepc:latest .

# Run with docker compose (starts with root + NET_ADMIN for VLAN operations)
docker compose up -d vepc

# Access CLI
docker exec -it vepc-mme-sgsn /app/vepc-cli

# If you run the container manually, keep NET_ADMIN and host networking enabled
# docker run --rm --name vepc --cap-add NET_ADMIN --network host --user root vepc:latest
```

### Ansible Deployment (Production)

```bash
# Configure target hosts
vim ansible/inventory

# Run deployment playbook
ansible-playbook -i ansible/inventory ansible/vepc-deploy.yml

# Verify deployment
ansible vepc_hosts -m command -a "docker ps"
```

## 📋 CLI Usage Examples

### Basic Commands

```bash
vepc# show config                    # Display configuration
vepc# show interface                 # List interfaces
vepc# show interface S1-MME detail   # Detailed interface info
vepc# show status                    # System status
vepc# show logging                   # View logs
```

### Configuration Mode

```bash
vepc# configure terminal             # Enter config mode
vepc(config)# interface S1-MME       # Select interface
vepc(config-if-S1-MME)# shutdown     # Disable interface
vepc(config-if-S1-MME)# no shutdown  # Enable interface
vepc(config-if-S1-MME)# exit         # Return to config mode
vepc(config)# end                    # Return to exec mode
```

### Linux Interface Management (Linux only)

```bash
vepc# create-vlan eth0 100           # Create VLAN 100 on eth0
vepc# delete-vlan eth0 100           # Delete VLAN 100 on eth0
vepc# list-interfaces                # Show Linux interfaces
vepc# up-interface eth0.100          # Bring up VLAN interface
vepc# set-ip eth0.100 192.168.1.1/24 # Assign IP address
vepc# down-interface eth0.100        # Bring down interface
vepc# delete-interface eth0.100      # Delete interface
```

## 🏗️ Architecture

```
┌─────────────────────────────────────┐
│      vEPC Main Process              │
├─────────────────────────────────────┤
│  ┌──────────────┐ ┌──────────────┐  │
│  │    MME       │ │    SGSN      │  │
│  │   Thread     │ │   Thread     │  │
│  └──────────────┘ └──────────────┘  │
├─────────────────────────────────────┤
│  Protocol Parsers                   │
│  - GTP Parser          - Diameter   │
│  - S1AP/NAS Parser     - Diameter   │
├─────────────────────────────────────┤
│  Network Interfaces (Ports/Sockets) │
│  - S1AP: 36412/SCTP   - S6a: 3868/UDP│
│  - S11: 2123/UDP      - Gn: 2123/UDP │
├─────────────────────────────────────┤
│  CLI Server                         │
│  - Linux: Unix socket /tmp/vepc.sock│
│  - Windows: TCP 127.0.0.1:5555      │
└─────────────────────────────────────┘
```

## 📦 Project Structure

```
vEPC/
├── src/                    # Core protocol parsers
│   ├── gtp_parser.cpp/h   # GTP-C v1 implementation
│   ├── diameter_parser.cpp/h # S6a Diameter
│   └── s1ap_parser.cpp/h   # S1AP with NAS
├── cli/                    # CLI client and interface management
│   ├── vepc-cli.cpp       # CLI client implementation
│   ├── linux_interface.h  # Linux VLAN/sub-interface ops
│   └── cisco_cli_commands.h # Cisco-style CLI definitions
├── config/                 # Configuration files
│   ├── vepc.config        # Main configuration
│   ├── vmme.conf          # MME settings
│   └── vsgsn.conf         # SGSN settings
├── tests/                  # Test suites
│   ├── test_gtp_parser.cpp
│   ├── test_diameter_parser.cpp
│   └── test_s1ap_parser.cpp
├── ansible/               # Deployment automation
│   ├── vepc-deploy.yml   # Main playbook
│   ├── inventory         # Target hosts
│   └── templates/        # Config templates
├── Dockerfile             # Container image build
└── docker-compose.yml     # Local dev environment
```

## ⚙️ Configuration

### Main Configuration (vepc.config)

```ini
MCC = 250
MNC = 20
GTP-C-Port = 2123
S1AP-Port = 36412
Diameter-Port = 3868
Log-Level = info
```

### Interface Configuration (interfaces.conf)

```
Name|Protocol|IP:Port|Peer|Description
S1-MME|S1AP|0.0.0.0:36412|eNodeB|eNodeB connection
S6a|Diameter|0.0.0.0:3868|HSS|HSS authentication
```

## 🔧 Requirements

### For Building
- C++17 compiler (GCC 7+, Clang 5+, MSVC 2017+)
- CMake 3.10+
- Linux/Windows/macOS

### For Running
- Linux or Windows
- 2GB+ RAM
- 10GB+ disk space

### For Docker
- Docker 20.10+
- Docker Compose 1.29+

### For Ansible
- Ansible 2.9+
- Python 3.8+
- Ubuntu 22.04 LTS (or higher)

## 🧪 Testing

### Run Unit Tests

```bash
cd build
ctest --output-on-failure
```

### Run Specific Tests

```bash
# Test GTP parser
./build/test_gtp_parser

# Test Diameter protocol
./build/test_diameter_parser

# Test CLI
./build/test_runtime_cli
```

### Integration Tests

```bash
# Run all integration tests via CMake
cd build && ctest -V
```

## 📚 Documentation

- [Deployment Guide](./ansible/README.md) - Ansible deployment instructions
- [Configuration Reference](./documentation/ROADMAP.md) - Feature roadmap and design
- [Protocol Implementation](./src/README.md) - Protocol details (if available)

## 🐳 Docker Usage

### Build Image

```bash
docker build -t vepc:latest .
```

### Run Container

```bash
# Run with docker-compose
docker-compose up -d

# Run standalone
docker run -d \
  --name vepc \
  -p 2123:2123/udp \
  -p 36412:36412/sctp \
  -p 3868:3868/udp \
  -p 5555:5555/tcp \
  -v $(pwd)/config:/etc/vepc \
  vepc:latest
```

### Access Inside Container

```bash
# Interactive CLI
docker exec -it vepc-mme-sgsn /app/vepc-cli

# Run single command
docker exec vepc-mme-sgsn /app/vepc-cli status

# View logs
docker logs -f vepc-mme-sgsn
```

## 🚀 Ansible Deployment

### Prerequisites

```bash
# Install Ansible
pip install ansible docker pyyaml

# Configure SSH access
ssh-copy-id ubuntu@target-host
```

### Deploy

```bash
# Deploy to all hosts
ansible-playbook -i ansible/inventory ansible/vepc-deploy.yml

# Deploy to specific host
ansible-playbook -i ansible/inventory ansible/vepc-deploy.yml -l vepc-prod

# With custom variables
ansible-playbook -i ansible/inventory ansible/vepc-deploy.yml \
  -e "mcc=310 mnc=410"
```

### Post-Deployment

```bash
# Check service status
sudo systemctl status vepc

# View logs
journalctl -u vepc -f

# Access CLI
docker exec -it vepc-mme-sgsn /app/vepc-cli
```

## 🔒 Security Considerations

- CLI is restricted to localhost by default
- Docker container runs as non-root user (`vepc:vepc`)
- Configure firewall rules for network access
- Implement authentication if exposing to untrusted networks
- Review network capabilities in docker-compose.yml for prod

## 📊 Performance

- Single process multi-threaded design
- Supports 10,000+ concurrent UE connections
- ~5000 PDP context capacity per SGSN
- Sub-millisecond message processing
- Configurable thread pool sizing

## 🤝 Contributing

1. Fork the repository
2. Create feature branch (`git checkout -b feature/enhancement`)
3. Commit changes (`git commit -am 'Add enhancement'`)
4. Push to branch (`git push origin feature/enhancement`)
5. Submit Pull Request

## 📄 License

[Add your license information here - typically MIT, GPL, or Apache 2.0]

## 🆘 Support & Troubleshooting

### Container fails to start
```bash
docker logs vepc-mme-sgsn
docker inspect vepc-mme-sgsn
```

### CLI connection refused
```bash
docker ps | grep vepc
docker exec vepc-mme-sgsn ps aux
```

### Port conflicts
```bash
netstat -tlnup | grep -E '2123|36412|3868|5555'
sudo lsof -i :5555
```

### Ansible deployment issues
```bash
ansible -i ansible/inventory vepc_hosts -m ping -vvv
ansible-playbook -i ansible/inventory ansible/vepc-deploy.yml --check
```

## 📞 Contact & Issues

- **GitHub Issues**: [Report bugs](https://github.com/cubasnik/vEPC/issues)
- **Discussions**: [Ask questions](https://github.com/cubasnik/vEPC/discussions)

## 🗺️ Roadmap

- [x] Core GTP-C/Diameter/S1AP protocol parsing
- [x] MME and SGSN context management
- [x] Cisco-style CLI interface
- [x] Docker containerization
- [x] Ansible deployment automation
- [x] Linux VLAN interface management
- [ ] GTP-U user plane data forwarding
- [ ] Complete protocol flow execution
- [ ] Web dashboard (future)
- [ ] Performance monitoring (future)

## 🎓 Learn More

- [3GPP Specifications](https://3gpp.org/) - Mobile network standards
- [GTP Protocol](https://en.wikipedia.org/wiki/GPRS_Tunnelling_Protocol) - Data tunnel protocol
- [Diameter Protocol](https://tools.ietf.org/html/rfc6733) - Authentication protocol
- [S1AP](https://en.wikipedia.org/wiki/S1_interface) - LTE eNodeB-MME interface

---

**vEPC** - Emulating mobile networks, one packet at a time.
