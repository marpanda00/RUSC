# Deploy RUSC backend as Docker on EC2 (container host at ec2-34-203-233-210)
# Usage: cd backend; .\deploy-ecs-docker.ps1

$ErrorActionPreference = "Stop"

$Ec2User = "ubuntu"
$Ec2Host = "ec2-34-203-233-210.compute-1.amazonaws.com"
$Ec2Key = "$env:USERPROFILE\.ssh\RUSC_KEY.pem"
$RemoteDir = "/home/ubuntu/rusc-docker"
$BackendRoot = $PSScriptRoot
$ImageName = "rusc-backend:latest"

if (-not (Test-Path $Ec2Key)) { Write-Error "SSH key not found: $Ec2Key" }

Write-Host "Docker deploy -> ${Ec2User}@${Ec2Host}" -ForegroundColor Cyan

$staging = Join-Path $env:TEMP "rusc-docker-build"
if (Test-Path $staging) { Remove-Item $staging -Recurse -Force }
New-Item -ItemType Directory -Path $staging | Out-Null

$items = @(
  "Dockerfile", ".dockerignore", "docker-compose.yml",
  "server.js", "package.json", "package-lock.json",
  "live-config.json", "boat-models.json", "boat-types.json",
  "boat-profiles.json", "device-names.json"
)
foreach ($name in $items) {
  $src = Join-Path $BackendRoot $name
  if (Test-Path $src) { Copy-Item $src $staging }
}
Copy-Item (Join-Path $BackendRoot "public") (Join-Path $staging "public") -Recurse
Copy-Item (Join-Path $BackendRoot "scripts") (Join-Path $staging "scripts") -Recurse

$archive = Join-Path $env:TEMP "rusc-docker-build.tar.gz"
if (Test-Path $archive) { Remove-Item $archive -Force }
Push-Location $staging
tar -czf $archive .
Pop-Location

Write-Host "Uploading build context..."
scp -i $Ec2Key $archive "${Ec2User}@${Ec2Host}:/tmp/rusc-docker-build.tar.gz"

$remoteCmd = @(
  "set -e"
  "sudo apt-get update -qq"
  "sudo apt-get install -y -qq docker.io docker-compose-v2 2>/dev/null || sudo apt-get install -y -qq docker.io"
  "sudo usermod -aG docker ubuntu 2>/dev/null || true"
  "mkdir -p $RemoteDir"
  "cd $RemoteDir"
  "tar -xzf /tmp/rusc-docker-build.tar.gz"
  "rm -f /tmp/rusc-docker-build.tar.gz"
  "sudo docker build -t $ImageName ."
  "pm2 stop rusc-backend 2>/dev/null || true"
  "sudo docker rm -f rusc-backend 2>/dev/null || true"
  "sudo docker run -d --name rusc-backend --restart unless-stopped -p 3000:3000 -p 3001:3001 -e LIVE_WS_ATTACH_HTTP=1 $ImageName"
  "sleep 3"
  "sudo docker ps --filter name=rusc-backend"
  "curl -sf http://127.0.0.1:3000/api/health && echo ' health OK'"
) -join "; "

ssh -i $Ec2Key "${Ec2User}@${Ec2Host}" $remoteCmd

Remove-Item $staging -Recurse -Force -ErrorAction SilentlyContinue
Remove-Item $archive -Force -ErrorAction SilentlyContinue

Write-Host ""
Write-Host "Done (Docker on EC2)." -ForegroundColor Green
Write-Host "  http://${Ec2Host}:3000/viewer/"
Write-Host "  GPS TCP ${Ec2Host}:3001"
