# Docker / AWS ECS (optional — not used in production)

**Production deploy uses bare metal:** see [DEPLOY_EC2.md](./DEPLOY_EC2.md) and run `.\deploy.ps1`.

The files below (`Dockerfile`, `deploy-ecs-docker.ps1`) are optional for future use only.

## Quick deploy (Docker on EC2) — optional

```powershell
cd C:\Code\RUSC\backend
.\deploy-ecs-docker.ps1
```

Not recommended unless you explicitly want containers.

## Local Docker test

```powershell
cd backend
docker compose up --build
```

## AWS ECS (Fargate) — full cloud setup

### 1. Build and push to ECR

```bash
aws ecr create-repository --repository-name rusc-backend --region eu-south-1
aws ecr get-login-password --region eu-south-1 | docker login --username AWS --password-stdin ACCOUNT.dkr.ecr.eu-south-1.amazonaws.com

cd backend
docker build -t rusc-backend .
docker tag rusc-backend:latest ACCOUNT.dkr.ecr.eu-south-1.amazonaws.com/rusc-backend:latest
docker push ACCOUNT.dkr.ecr.eu-south-1.amazonaws.com/rusc-backend:latest
```

### 2. Task definition

Edit `ecs/task-definition.json`: replace `ACCOUNT_ID`, `REGION`, IAM role ARNs.

```bash
aws ecs register-task-definition --cli-input-json file://ecs/task-definition.json
```

### 3. Load balancers

| Traffic | AWS resource | Port |
|---------|----------------|------|
| Browser / API / WebSocket | **ALB** → target group | 3000 |
| GPS devices (TCP) | **NLB** → target group | 3001 |

WebSocket uses the **same port as HTTP** (`LIVE_WS_ATTACH_HTTP=1` in the image).

### 4. ECS service

- Launch type: **Fargate**
- Subnets: public or private + NAT
- Security groups: allow ALB → 3000, NLB → 3001
- Service discovery optional

### 5. TLS

Terminate HTTPS on the **ALB** (ACM certificate). Do not rely on in-container port 8443.

## Environment variables

| Variable | Default | Meaning |
|----------|---------|---------|
| `API_PORT` | 3000 | HTTP + static files + live WebSocket |
| `TCP_PORT` | 3001 | GPS gateway TCP |
| `LIVE_WS_ATTACH_HTTP` | 1 in Docker | `0` = separate WS port 3002 (local dev) |

## Security group (EC2 or ECS)

Inbound: **22**, **3000**, **3001** (and **443** on ALB for production).

## Roll back to pm2

```bash
ssh -i ~/.ssh/RUSC_KEY.pem ubuntu@ec2-34-203-233-210.compute-1.amazonaws.com
sudo docker rm -f rusc-backend
cd ~/backend && pm2 start server.js --name rusc-backend
```
