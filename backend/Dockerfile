FROM node:20-bookworm-slim

WORKDIR /app

COPY package.json package-lock.json ./
RUN npm install --omit=dev --no-audit --no-fund

COPY server.js ./
COPY live-config.json boat-models.json boat-types.json boat-profiles.json device-names.json ./
COPY public ./public
COPY scripts ./scripts

ENV NODE_ENV=production
ENV API_PORT=3000
ENV TCP_PORT=3001
ENV LIVE_WS_PORT=3002
ENV LIVE_WS_ATTACH_HTTP=1

EXPOSE 3000 3001

HEALTHCHECK --interval=30s --timeout=5s --start-period=15s --retries=3 \
  CMD node -e "require('http').get('http://127.0.0.1:3000/api/health',(r)=>{process.exit(r.statusCode===200?0:1)}).on('error',()=>process.exit(1))"

CMD ["node", "server.js"]
