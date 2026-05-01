-- Verify enclave-station:0001-initial-schema on pg
-- Sentinel checks: a representative slice of the 54 application tables must exist.
-- If any of these is missing, deploy didn't complete.

BEGIN;

SELECT 1/COUNT(*) FROM pg_tables WHERE schemaname='public' AND tablename='users';
SELECT 1/COUNT(*) FROM pg_tables WHERE schemaname='public' AND tablename='channels';
SELECT 1/COUNT(*) FROM pg_tables WHERE schemaname='public' AND tablename='messages';
SELECT 1/COUNT(*) FROM pg_tables WHERE schemaname='public' AND tablename='sessions';
SELECT 1/COUNT(*) FROM pg_tables WHERE schemaname='public' AND tablename='spaces';
SELECT 1/COUNT(*) FROM pg_tables WHERE schemaname='public' AND tablename='webauthn_credentials';
SELECT 1/COUNT(*) FROM pg_tables WHERE schemaname='public' AND tablename='password_credentials';
SELECT 1/COUNT(*) FROM pg_tables WHERE schemaname='public' AND tablename='totp_credentials';
SELECT 1/COUNT(*) FROM pg_tables WHERE schemaname='public' AND tablename='user_keys';
SELECT 1/COUNT(*) FROM pg_tables WHERE schemaname='public' AND tablename='server_settings';

ROLLBACK;
