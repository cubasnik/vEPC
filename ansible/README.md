# vEPC Ansible Deployment Guide

This directory contains Ansible playbooks and configurations for deploying vEPC (Evolved Packet Core Platform) on Ubuntu 22.04 LTS and higher.

## Prerequisites

### On Your Local Machine (Ansible Control Node)
- Ansible 2.9 or higher
- Python 3.8+
- SSH access to target hosts
- Git

### On Target Machines (Ubuntu 22.04+)
- SSH server running
- `sudo` privileges (or ability to provide sudo password)
- Minimum 2 GB RAM (4 GB recommended)
- Minimum 10 GB disk space
- Network connectivity to GitHub

## Installation

### 1. Install Ansible (on control node)

```bash
# Ubuntu/Debian
sudo apt-get update
sudo apt-get install -y ansible python3-pip
pip3 install docker pyyaml

# macOS (with Homebrew)
brew install ansible
pip3 install docker pyyaml

# Or use pip directly
pip3 install ansible==2.10.0 docker pyyaml
```

### 2. Configure SSH Access

```bash
# Generate SSH key (if not already done)
ssh-keygen -t rsa -b 4096 -f ~/.ssh/id_rsa

# Copy SSH key to target host
ssh-copy-id -i ~/.ssh/id_rsa.pub ubuntu@target-host-ip

# Test SSH connection
ssh -i ~/.ssh/id_rsa ubuntu@target-host-ip
```

## Configuration

### 1. Update Inventory File

Edit `ansible/inventory` to specify your target hosts:

```ini
[vepc_hosts]
vepc-prod ansible_host=192.168.1.100 ansible_user=ubuntu ansible_ssh_private_key_file=~/.ssh/id_rsa
vepc-test ansible_host=192.168.1.101 ansible_user=ubuntu ansible_ssh_private_key_file=~/.ssh/id_rsa
```

### 2. Customize Variables

Edit `ansible/inventory` to customize deployment variables:

```bash
[vepc_hosts:vars]
vepc_repo_url=https://github.com/your-org/vEPC.git
vepc_repo_branch=main
mcc=250
mnc=20
gtp_c_port=2123
s1ap_port=36412
diameter_port=3868
cli_port=5555
traffic_linux_ports=["ens160","eth1"]
```

`traffic_linux_ports` is required and defines Linux interfaces allowed for traffic operations (`bind`, `create-vlan`) to prevent accidental changes on management interfaces.

Or override variables at runtime:

```bash
ansible-playbook -i ansible/inventory vepc-deploy.yml \
  -e "mcc=310 mnc=410 vepc_repo_branch=develop"
```

## Deployment

### 1. Validate Playbook

```bash
# Check syntax
ansible-playbook -i ansible/inventory vepc-deploy.yml --syntax-check

# Perform a dry-run
ansible-playbook -i ansible/inventory vepc-deploy.yml --check
```

### 2. Execute Deployment

```bash
# Deploy to all hosts
ansible-playbook -i ansible/inventory vepc-deploy.yml

# Deploy to specific host
ansible-playbook -i ansible/inventory vepc-deploy.yml -l vepc-prod

# Deploy with verbose output
ansible-playbook -i ansible/inventory vepc-deploy.yml -vvv
```

### 3. Prompt for Sudo Password (if needed)

```bash
ansible-playbook -i ansible/inventory vepc-deploy.yml -K
```

## Post-Deployment Verification

### Check Container Status

```bash
# SSH to target machine
ssh ubuntu@target-host-ip

# Check if container is running
docker ps | grep vepc

# View container logs
docker logs -f vepc-mme-sgsn

# Check service status
sudo systemctl status vepc
```

### Verify Service Functionality

```bash
# Access CLI
docker exec -it vepc-mme-sgsn /app/vepc-cli

# Inside CLI, run:
vepc# show status
vepc# show interface
vepc# show config
```

### Test Network Connectivity

```bash
# Check if ports are listening
sudo netstat -tlnup | grep -E '2123|36412|3868|5555'

# Or using ss
sudo ss -tlnup | grep -E '2123|36412|3868|5555'

# Test Diameter connection (if HSS available)
timeout 3 bash -c 'echo > /dev/tcp/127.0.0.1/3868' && echo "Diameter port is open" || echo "Diameter port is closed"
```

## Troubleshooting

### Ansible Connection Issues

```bash
# Test connectivity
ansible -i ansible/inventory vepc_hosts -m ping

# Verbose SSH debugging
ansible-playbook -i ansible/inventory vepc-deploy.yml -vvv -e "ansible_user=ubuntu"
```

### Docker Issues

```bash
# Check Docker service
sudo systemctl status docker

# Restart Docker
sudo systemctl restart docker

# View Docker daemon logs
sudo journalctl -u docker -f
```

### Port Conflicts

```bash
# Check what's using the ports
sudo lsof -i :2123,36412,3868,5555

# Or
sudo netstat -tlnup | grep LISTEN
```

### Container Fails to Start

```bash
# View detailed logs
docker logs vepc-mme-sgsn

# Inspect container
docker inspect vepc-mme-sgsn

# Check available disk space
df -h

# Check memory
free -m
```

## Advanced Tasks

### Update Repository

```bash
# Update code without redeploying
ansible-playbook -i ansible/inventory vepc-deploy.yml \
  -e "vepc_repo_branch=develop" \
  --tags "repository,docker_build,restart"
```

### Update Configuration

```bash
# Redeploy configuration only
ansible-playbook -i ansible/inventory vepc-deploy.yml \
  --tags "config,restart" \
  -e "log_level=debug"
```

### Scale Deployment

To deploy multiple instances:

```ini
[vepc_hosts]
vepc-1 ansible_host=192.168.1.100 container_name=vepc-instance-1
vepc-2 ansible_host=192.168.1.101 container_name=vepc-instance-2
vepc-3 ansible_host=192.168.1.102 container_name=vepc-instance-3
```

### Rollback

```bash
# Stop and remove containers
docker-compose -f /opt/vepc/docker-compose.yml down

# Restore previous git commit
cd /opt/vepc
git checkout <previous-commit-hash>

# Rerun playbook
ansible-playbook -i ansible/inventory vepc-deploy.yml
```

## CI/CD Integration

### GitHub Actions Example

```yaml
name: Deploy vEPC

on: [push]

jobs:
  deploy:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v2
      
      - name: Install Ansible
        run: pip install ansible docker pyyaml
      
      - name: Deploy vEPC
        run: |
          ansible-playbook -i ansible/inventory vepc-deploy.yml \
            -e "vepc_repo_branch=${{ github.ref }}" \
            -K
        env:
          ANSIBLE_HOST_KEY_CHECKING: false
```

## Maintenance

### Regular Updates

```bash
# Update from GitHub weekly
0 0 * * 0 cd /opt/vepc && git pull origin main && docker-compose restart

# Or via Ansible
ansible-playbook -i ansible/inventory vepc-deploy.yml \
  --tags "repository,docker_build,restart"
```

### Log Rotation

Logs are automatically rotated by Docker (max 10MB per file, 3 files).

View deployment info:
```bash
sudo cat /var/log/vepc/deployment_info.txt
```

## Uninstall

To remove vEPC deployment:

```bash
# Stop service
sudo systemctl stop vepc

# Remove container
docker-compose -f /opt/vepc/docker-compose.yml down -v

# Remove Docker image
docker rmi <vepc_docker_image>

# Remove systemd service
sudo rm /etc/systemd/system/vepc.service
sudo systemctl daemon-reload

# Remove installation directory (optional)
sudo rm -rf /opt/vepc /etc/vepc /var/log/vepc
```

## Support

For issues and questions:
1. Check the [vEPC GitHub Issues](https://github.com/your-org/vEPC/issues)
2. Review [deployment logs](#view-container-logs-follow)
3. Check [systemd logs](#view-systemd-logs)
4. Reference the main [vEPC documentation](../documentation/)

## License

Ansible playbooks are provided under the same license as vEPC project.
