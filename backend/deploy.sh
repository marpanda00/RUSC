#!/bin/bash
# RUSC backend — deploy to EC2 (Ubuntu + pm2)
set -e

EC2_USER="ubuntu"
EC2_HOST="ec2-34-203-233-210.compute-1.amazonaws.com"
EC2_KEY="${EC2_KEY:-$HOME/.ssh/RUSC_KEY.pem}"
DEPLOY_PATH="/home/ubuntu/backend"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

if [[ ! -f "$EC2_KEY" ]]; then
  echo "SSH key not found: $EC2_KEY"
  exit 1
fi

echo "Deploying to ${EC2_USER}@${EC2_HOST}:${DEPLOY_PATH}"

STAGING="$(mktemp -d)"
trap 'rm -rf "$STAGING"' EXIT

for f in server.js package.json package-lock.json live-config.json \
  boat-models.json boat-types.json boat-profiles.json device-names.json .env.example; do
  [[ -f "$SCRIPT_DIR/$f" ]] && cp "$SCRIPT_DIR/$f" "$STAGING/"
done
cp -a "$SCRIPT_DIR/public" "$SCRIPT_DIR/scripts" "$STAGING/"

ARCHIVE="/tmp/rusc-backend-deploy-$$.tar.gz"
tar -czf "$ARCHIVE" -C "$STAGING" .
echo "Uploading $(du -h "$ARCHIVE" | cut -f1)..."
scp -i "$EC2_KEY" "$ARCHIVE" "${EC2_USER}@${EC2_HOST}:/tmp/rusc-backend-deploy.tar.gz"

ssh -i "$EC2_KEY" "${EC2_USER}@${EC2_HOST}" bash -s <<EOF
set -e
mkdir -p $DEPLOY_PATH
cd $DEPLOY_PATH
tar -xzf /tmp/rusc-backend-deploy.tar.gz
rm -f /tmp/rusc-backend-deploy.tar.gz
[[ -f .env ]] || cp .env.example .env
npm install --production
if pm2 describe rusc-backend >/dev/null 2>&1; then
  pm2 restart rusc-backend
else
  pm2 start server.js --name rusc-backend
fi
pm2 save
ss -tlnp | grep -E '3000|3001|3002|8443' || true
EOF

rm -f "$ARCHIVE"
echo ""
echo "Done."
echo "  http://${EC2_HOST}:3000/viewer/"
