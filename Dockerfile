# Multi-stage build for vEPC - Evolved Packet Core Emulator
# Stage 1: Build stage
FROM ubuntu:22.04 AS builder

# Install build dependencies
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    git \
    libreadline-dev \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

# Set working directory
WORKDIR /build

# Use source files from build context
COPY . /build

# Build the project
RUN mkdir -p build && cd build && \
    cmake .. && \
    make -j$(nproc)

# Stage 2: Runtime stage
FROM ubuntu:22.04

# Install runtime dependencies
RUN apt-get update && apt-get install -y --no-install-recommends \
    iproute2 \
    net-tools \
    tcpdump \
    curl \
    libreadline8 \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

# Create non-root user
RUN useradd -m -u 1000 vepc && \
    mkdir -p /app /etc/vepc /var/log/vepc && \
    chown -R vepc:vepc /app /etc/vepc /var/log/vepc

WORKDIR /app

# Copy built binaries from builder
COPY --from=builder /build/build/vepc /app/vepc
COPY --from=builder /build/build/vepc-cli /app/vepc-cli
COPY --from=builder /build/config/* /etc/vepc/

# Change ownership
RUN chown -R vepc:vepc /app /etc/vepc

# Keep root as the runtime user so CAP_NET_ADMIN is effective for VLAN/bind operations.
# The dedicated vepc user remains available for environments that do not need NIC management.

# Create a health check script
RUN mkdir -p /app/scripts

# Expose ports
# GTP-C: 2123/UDP
# S1AP: 36412/SCTP  
# Diameter S6a: 3868/UDP
# CLI: 5555/TCP (when run with TCP mode)
EXPOSE 2123/udp 36412/sctp 3868/udp 5555/tcp

# Configure volumes
VOLUME ["/etc/vepc", "/var/log/vepc"]

# Default command - start vEPC in foreground
CMD ["/app/vepc", "--config=/etc/vepc"]

# Health check (adjust as needed)
HEALTHCHECK --interval=30s --timeout=10s --start-period=5s --retries=3 \
    CMD /app/vepc-cli status | grep -q "UP" || exit 1
