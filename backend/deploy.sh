#!/bin/bash

# RUSC Backend Deployment Script
# Deploy backend to AWS EC2 server

set -e

EC2_USER="ubuntu"
EC2_HOST="ec2-34-203-233-210.compute-1.amazonaws.com"
EC2_KEY="$HOME/.ssh/RUSC_KEY.pem"
DEPLOY_PATH="/home/ubuntu/backend"

echo "🚀 Starting RUSC Backend Deployment"
echo "Target: $EC2_USER@$EC2_HOST:$DEPLOY_PATH"
echo ""

# 1. Create remote directory if it doesn't exist
echo "📁 Creating remote deployment directory..."
ssh -i "$EC2_KEY" "$EC2_USER@$EC2_HOST" "mkdir -p $DEPLOY_PATH"

# 2. Copy backend files
echo "📦 Transferring backend files..."
scp -i "$EC2_KEY" -r server.js package.json package-lock.json .env.production "$EC2_USER@$EC2_HOST:$DEPLOY_PATH/"

# 3. Install dependencies on remote server
echo "📥 Installing dependencies..."
ssh -i "$EC2_KEY" "$EC2_USER@$EC2_HOST" "cd $DEPLOY_PATH && npm install --production"

# 4. Instructions for running
echo ""
echo "✅ Deployment Complete!"
echo ""
echo "To start the backend on the EC2 server, SSH in and run:"
echo "  ssh -i $EC2_KEY $EC2_USER@$EC2_HOST"
echo "  cd $DEPLOY_PATH"
echo "  npm start"
echo ""
echo "Or use a process manager like pm2:"
echo "  npm install -g pm2"
echo "  pm2 start server.js --name 'rusc-backend'"
echo ""
echo "Gateway will connect to: ec2-34-203-233-210.compute-1.amazonaws.com:3001"
