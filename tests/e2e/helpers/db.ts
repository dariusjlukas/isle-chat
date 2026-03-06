/**
 * Database helpers for E2E tests.
 * Resets all application data between tests via docker exec into the PostgreSQL container.
 */

import { execSync } from "child_process";

const PG_USER = process.env.POSTGRES_USER ?? "chatapp_test";
const PG_DB = process.env.POSTGRES_DB ?? "chatapp_test";
const PG_CONTAINER =
  process.env.TEST_PG_CONTAINER ?? "chatapp-test-postgres";

export function resetDatabase(): void {
  const sql = `
    DO \\$\\$
    DECLARE r RECORD;
    BEGIN
      FOR r IN
        SELECT tablename FROM pg_tables
        WHERE schemaname = 'public'
          AND tablename NOT IN ('schema_migrations')
      LOOP
        EXECUTE 'TRUNCATE TABLE ' || quote_ident(r.tablename) || ' CASCADE';
      END LOOP;
    END \\$\\$;
  `;
  execSync(
    `docker exec ${PG_CONTAINER} psql -U "${PG_USER}" -d "${PG_DB}" -c "${sql}"`,
    { stdio: "pipe" },
  );
}
