# Deploy RUSC backend to EC2 (bare metal + pm2)

**Default:** Node.js on the server with **pm2** — not Docker.

Production host (Ubuntu):

| Item | Value |
|------|--------|
| Host | `ec2-34-203-233-210.compute-1.amazonaws.com` |
| User | `ubuntu` |
| SSH key | `C:\Users\mario\.ssh\RUSC_KEY.pem` |
| App path | `/home/ubuntu/backend` |
| Process | `pm2` → `rusc-backend` |

## Deploy from Windows

```powershell
cd C:\Code\RUSC\backend
.\deploy.ps1
```

## Deploy from Git Bash / WSL

```bash
cd backend
./deploy.sh
```

## Ports (open in EC2 security group)

| Port | Protocol | Use |
|------|----------|-----|
| 3000 | TCP | HTTP API + static site (`/viewer/`, `/regatta-viewer/`) |
| 3001 | TCP | GPS gateway (device TCP ingest) |
| 3002 | TCP | Live 3D WebSocket |
| 8443 | TCP | HTTPS (optional; uses `private.key` + `certificate.crt` on server) |
| 22 | TCP | SSH |

## URLs after deploy

- Live 3D: `http://ec2-34-203-233-210.compute-1.amazonaws.com:3000/viewer/`
- Recorded replay: `http://ec2-34-203-233-210.compute-1.amazonaws.com:3000/regatta-viewer/`
- API health: `http://ec2-34-203-233-210.compute-1.amazonaws.com:3000/api/health`

## Server management

```bash
ssh -i ~/.ssh/RUSC_KEY.pem ubuntu@ec2-34-203-233-210.compute-1.amazonaws.com

pm2 list
pm2 logs rusc-backend
pm2 restart rusc-backend
```

TLS certs (`private.key`, `certificate.crt`) live only on the server — deploy does **not** overwrite them.

## AWS ECS note

This instance is **EC2 + Node + pm2**, not an ECS cluster (no Docker/ECS agent). To move to real ECS later: add a `Dockerfile`, push to ECR, and run as an ECS service behind ALB/NLB.
