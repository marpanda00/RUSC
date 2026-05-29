# RUSC backend — deploy to EC2 (Ubuntu + pm2)
# Usage: cd backend; .\deploy.ps1

$ErrorActionPreference = "Stop"

$Ec2User = "ubuntu"
$Ec2Host = "ec2-34-203-233-210.compute-1.amazonaws.com"
$Ec2Key = "$env:USERPROFILE\.ssh\RUSC_KEY.pem"
$DeployPath = "/home/ubuntu/backend"
$BackendRoot = $PSScriptRoot

if (-not (Test-Path $Ec2Key)) {
    Write-Error "SSH key not found: $Ec2Key"
}

Write-Host "Deploying RUSC backend to ${Ec2User}@${Ec2Host}:${DeployPath}" -ForegroundColor Cyan

$staging = Join-Path $env:TEMP "rusc-backend-deploy"
if (Test-Path $staging) { Remove-Item $staging -Recurse -Force }
New-Item -ItemType Directory -Path $staging | Out-Null

$copyItems = @(
    "server.js", "package.json", "package-lock.json",
    "live-config.json", "boat-models.json", "boat-types.json",
    "boat-profiles.json", "device-names.json", ".env.example"
)
foreach ($name in $copyItems) {
    $src = Join-Path $BackendRoot $name
    if (Test-Path $src) { Copy-Item $src $staging }
}

Copy-Item (Join-Path $BackendRoot "public") (Join-Path $staging "public") -Recurse
Copy-Item (Join-Path $BackendRoot "scripts") (Join-Path $staging "scripts") -Recurse

$archive = Join-Path $env:TEMP "rusc-backend-deploy.tar.gz"
if (Test-Path $archive) { Remove-Item $archive -Force }
Push-Location $staging
tar -czf $archive .
Pop-Location

Write-Host "Uploading archive ($([math]::Round((Get-Item $archive).Length / 1MB, 1)) MB)..."
scp -i $Ec2Key $archive "${Ec2User}@${Ec2Host}:/tmp/rusc-backend-deploy.tar.gz"

$remoteCmd = "set -e; mkdir -p $DeployPath; cd $DeployPath; " +
  "tar -xzf /tmp/rusc-backend-deploy.tar.gz; rm -f /tmp/rusc-backend-deploy.tar.gz; " +
  "test -f .env || cp .env.example .env; npm install --production; " +
  "if pm2 describe rusc-backend >/dev/null 2>&1; then pm2 restart rusc-backend; " +
  "else pm2 start server.js --name rusc-backend; fi; pm2 save; " +
  "ss -tlnp 2>/dev/null | grep -E '3000|3001|3002|8443' || true"

ssh -i $Ec2Key "${Ec2User}@${Ec2Host}" $remoteCmd

Remove-Item $staging -Recurse -Force -ErrorAction SilentlyContinue
Remove-Item $archive -Force -ErrorAction SilentlyContinue

Write-Host ""
Write-Host "Done." -ForegroundColor Green
Write-Host "  HTTP API + site : http://${Ec2Host}:3000"
Write-Host "  HTTPS           : https://${Ec2Host}:8443"
Write-Host "  Live WebSocket  : ws://${Ec2Host}:3002"
Write-Host "  GPS TCP         : ${Ec2Host}:3001"
Write-Host "  3D viewer       : http://${Ec2Host}:3000/viewer/"
