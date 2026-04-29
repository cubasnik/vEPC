FROM node:20-alpine AS builder
WORKDIR /app
COPY package.json package-lock.json* ./
RUN npm install --legacy-peer-deps || true
COPY . .
RUN npm run build --workspace=. || npm run build

FROM node:20-alpine
WORKDIR /app
COPY --from=builder /app .
RUN npm ci --only=production || true
ENV NODE_ENV=production
EXPOSE 3000
CMD ["node", "api/server.js"]
