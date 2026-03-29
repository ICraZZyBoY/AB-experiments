# AB-experiments

Minimal scaffold for an A/B experiments platform.

## Stack

- PostgreSQL
- C++ backend on userver
- React frontend
- Docker Compose

## Project Structure

- `create_db.sql` - initial PostgreSQL schema
- `backend/` - minimal userver HTTP service
- `frontend/` - React + Vite frontend placeholder
- `docker-compose.yml` - local environment for all services

## Run

1. Copy `.env.example` to `.env` and set a local PostgreSQL password.
2. Start the project:

```bash
docker compose up --build
```

3. Open:

- Frontend: `http://localhost:3000`
- Backend health: `http://localhost:8080/health`
- Backend client services: `http://localhost:8080/api/v1/client-services`
- Backend monitor: `http://localhost:8081/service/monitor?format=json`
- PostgreSQL: `localhost:5432`

## Current Baseline

- PostgreSQL starts in Docker and applies `create_db.sql` on first initialization.
- Backend runs on userver, exposes health/auth endpoints, and reads/writes `ClientService`, `PlatformUser`, `PlatformRole`, and `ApiKey` data in PostgreSQL.
- Frontend opens a placeholder page, checks backend availability, and displays the number of loaded client services.

## Auth Flow

- `POST /api/v1/auth/register-service` creates a new client service and the first admin account.
- `POST /api/v1/auth/login` starts login by `email` and `password`; if several services match, backend returns a list of services and expects a second request with `client_service_id`.
- `GET /api/v1/platform-users` lists platform users for the current service for any authenticated platform user.
- `POST /api/v1/platform-users` creates a new platform user inside the current service for admins only.
- `GET /api/v1/api-keys` lists API keys for the current service for admins and developers.
- `POST /api/v1/api-keys` creates a new API key and returns the plain key once for admins only.
- `DELETE /api/v1/api-keys?api_key_id=<id>` revokes an API key for admins only.

Use `Authorization: Bearer <session_token>` for protected endpoints.

Current limitations:

- Session tokens are stored in backend process memory, so they are reset after service restart.
- Plain API keys are shown only once at creation time and are not stored by the frontend after page refresh.
