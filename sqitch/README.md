# Sqitch migrations

Schema lives here as versioned migrations. Backend boot still runs the legacy
`Database::init_schema()` by default (`ENABLE_SQITCH_ONLY=0`), so existing
deployments are unaffected. Sqitch deploys are additive: they produce the same
schema as `init_schema()` and can be enabled by setting `ENABLE_SQITCH_ONLY=1`
once production is comfortable with the rollout (PR-B).

## Layout

- `sqitch.conf` — engine config (postgres).
- `sqitch.plan` — ordered list of migrations.
- `deploy/` — forward migrations (apply).
- `revert/` — reverse migrations (rollback).
- `verify/` — read-only assertions run after each deploy.

## Initial migration

`deploy/0001-initial-schema.sql` was generated from a live database that had
been populated by `Database::init_schema()` against an empty Postgres 16:

```bash
docker run --rm -d --name pg-bootstrap -e POSTGRES_PASSWORD=bootstrap \
  -e POSTGRES_DB=enclave -p 5439:5432 postgres:16-alpine
POSTGRES_USER=postgres POSTGRES_PASSWORD=bootstrap POSTGRES_DB=enclave \
  POSTGRES_HOST=localhost POSTGRES_PORT=5439 BACKEND_PORT=9099 \
  UPLOAD_DIR=/tmp/uploads backend/build/chat-server &
# wait for /api/health
docker exec pg-bootstrap pg_dump -U postgres -d enclave \
  --schema-only --no-owner --no-privileges --no-comments \
  --quote-all-identifiers > deploy/0001-initial-schema.sql
```

To regenerate it after `init_schema()` changes (during P1.1 transition):
re-run the procedure above and replace the file. The `verify/` checks should
still pass.

## Local apply

```bash
docker compose --env-file .env.example --profile sqitch run --rm sqitch deploy --verify
```

## Rollback

```bash
docker compose --env-file .env.example --profile sqitch run --rm sqitch revert --to @HEAD^
```

`revert/0001-initial-schema.sql` does `DROP SCHEMA public CASCADE` — destructive.
Always back up first.
