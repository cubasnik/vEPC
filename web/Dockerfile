FROM node:20-alpine AS builder
WORKDIR /app
COPY package.json package-lock.json* ./
RUN npm install --legacy-peer-deps || true
COPY . .
RUN npm run build --workspace=. || npm run build
RUN if [ -f ./api/package.json ]; then cd api && npm install --legacy-peer-deps || true; fi

FROM node:20-alpine
WORKDIR /app
COPY --from=builder /app .
# api dependencies are installed in the builder stage (see below),
# no need to run `npm ci` here which may miss nested `api/package.json`.
ENV NODE_ENV=production
EXPOSE 3000
CMD ["node", "api/server.js"]
