-- Revert enclave-station:0001-initial-schema from pg
-- Drops the entire application schema. Destructive.

BEGIN;

DROP SCHEMA IF EXISTS "public" CASCADE;
CREATE SCHEMA "public";

COMMIT;
