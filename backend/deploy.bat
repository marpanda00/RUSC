@echo off
REM RUSC Backend Deployment Script for Windows
REM Deploy backend to AWS EC2 server

setlocal enabledelayedexpansion

set EC2_USER=ubuntu
set EC2_HOST=ec2-34-203-233-210.compute-1.amazonaws.com
set EC2_KEY=%USERPROFILE%\.ssh\RUSC_KEY.pem
set DEPLOY_PATH=/home/ubuntu/backend

echo.
echo 🚀 Starting RUSC Backend Deployment
echo Target: %EC2_USER%@%EC2_HOST%:%DEPLOY_PATH%
echo.

REM Check if SSH key exists
if not exist "%EC2_KEY%" (
    echo ❌ Error: SSH key not found at %EC2_KEY%
    echo Please ensure RUSC_KEY.pem is in your .ssh folder
    exit /b 1
)

REM 1. Create remote directory if it doesn't exist
echo 📁 Creating remote deployment directory...
ssh -i "%EC2_KEY%" %EC2_USER%@%EC2_HOST% "mkdir -p %DEPLOY_PATH%"
if errorlevel 1 (
    echo ❌ Failed to create remote directory
    exit /b 1
)

REM 2. Copy backend files
echo 📦 Transferring backend files...
scp -i "%EC2_KEY%" server.js "%EC2_USER%@%EC2_HOST%:%DEPLOY_PATH%/"
scp -i "%EC2_KEY%" package.json "%EC2_USER%@%EC2_HOST%:%DEPLOY_PATH%/"
scp -i "%EC2_KEY%" package-lock.json "%EC2_USER%@%EC2_HOST%:%DEPLOY_PATH%/"
scp -i "%EC2_KEY%" .env.production "%EC2_USER%@%EC2_HOST%:%DEPLOY_PATH%/.env"

if errorlevel 1 (
    echo ❌ Failed to transfer files
    exit /b 1
)

REM 3. Install dependencies on remote server
echo 📥 Installing dependencies...
ssh -i "%EC2_KEY%" %EC2_USER%@%EC2_HOST% "cd %DEPLOY_PATH% && npm install --production"

if errorlevel 1 (
    echo ❌ Failed to install dependencies
    exit /b 1
)

echo.
echo ✅ Deployment Complete!
echo.
echo To start the backend on the EC2 server, SSH in and run:
echo   ssh -i "%EC2_KEY%" %EC2_USER%@%EC2_HOST%
echo   cd %DEPLOY_PATH%
echo   npm start
echo.
echo Or use a process manager like pm2:
echo   npm install -g pm2
echo   pm2 start server.js --name 'rusc-backend'
echo.
echo Gateway will connect to: ec2-34-203-233-210.compute-1.amazonaws.com:3001
echo Website backend URL: http://ec2-34-203-233-210.compute-1.amazonaws.com:3000
echo.

endlocal
