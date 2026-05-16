#!/usr/bin/env bash

# RUSC Setup and Run on EC2
# Quick script to set up everything needed on the EC2 instance

set -e

echo "======================================"
echo "RUSC Backend Setup on EC2"
echo "======================================"
echo ""

# Update system
echo "📦 Updating system packages..."
sudo apt-get update
sudo apt-get upgrade -y

# Install Node.js if not already installed
if ! command -v node &> /dev/null; then
    echo "📥 Installing Node.js..."
    curl -fsSL https://deb.nodesource.com/setup_18.x | sudo -E bash -
    sudo apt-get install -y nodejs
fi

# Install PM2 for process management
echo "📥 Installing PM2..."
sudo npm install -g pm2

# Create backend directory
echo "📁 Creating backend directory..."
mkdir -p ~/backend
cd ~/backend

# Install dependencies (assumes server.js and package.json are already there)
if [ -f "package.json" ]; then
    echo "📥 Installing backend dependencies..."
    npm install --production
else
    echo "⚠️  Warning: package.json not found in ~/backend"
    echo "Please ensure deployment completed successfully"
    exit 1
fi

# Start the server with PM2
echo "🚀 Starting backend server with PM2..."
pm2 start server.js --name "rusc-backend"
pm2 save
pm2 startup

echo ""
echo "======================================"
echo "✅ Setup Complete!"
echo "======================================"
echo ""
echo "Backend server is now running on:"
echo "  HTTP API: ec2-34-203-233-210.compute-1.amazonaws.com:3000"
echo "  TCP GPS: ec2-34-203-233-210.compute-1.amazonaws.com:3001"
echo ""
echo "View logs: pm2 logs rusc-backend"
echo "Status: pm2 status"
echo ""
