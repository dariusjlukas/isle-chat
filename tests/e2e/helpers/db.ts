/**
 * Database helpers for E2E tests.
 * Resets all application data between tests via docker exec into the PostgreSQL container.
 */

import { execSync } from "child_process";

export interface DbConfig {
  pgUser: string;
  pgDb: string;
  pgContainer: string;
}

const defaultConfig: DbConfig = {
  pgUser: process.env.POSTGRES_USER ?? "chatapp_test",
  pgDb: process.env.POSTGRES_DB ?? "chatapp_test",
  pgContainer: process.env.TEST_PG_CONTAINER ?? "chatapp-test-postgres",
};

export function resetDatabase(config: DbConfig = defaultConfig): void {
  const { pgUser, pgDb, pgContainer } = config;
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
    `docker exec ${pgContainer} psql -U "${pgUser}" -d "${pgDb}" -c "${sql}"`,
    { stdio: "pipe" },
  );
}
