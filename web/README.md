# vEPC Web UI (placeholder)

This folder contains a minimal React + Vite frontend and a small Express adapter to serve the built static files and provide API routes.

How to run locally:

1. Install dependencies:

```bash
cd web
npm install
cd api
npm install
```

2. Dev server (frontend):

```bash
cd web
npm run dev
```

3. Build & run combined server:

```bash
cd web
npm run build
npm start
```

Docker (from repo root):

```bash
docker-compose up --build vepc-web
```

Branding: place your icon `logo.png` or `logo.svg` into `web/public/` replacing `logo-placeholder.svg`.
