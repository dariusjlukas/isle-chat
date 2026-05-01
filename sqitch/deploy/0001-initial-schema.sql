-- Deploy enclave-station:0001-initial-schema to pg
-- Initial schema captured from Database::init_schema() output.
-- See README in sqitch/ for how this was generated.

BEGIN;

--
-- PostgreSQL database dump
--



SET statement_timeout = 0;
SET lock_timeout = 0;
SET idle_in_transaction_session_timeout = 0;
SET client_encoding = 'UTF8';
SET standard_conforming_strings = on;
SELECT pg_catalog.set_config('search_path', '', false);
SET check_function_bodies = false;
SET xmloption = content;
SET client_min_messages = warning;
SET row_security = off;

--
-- Name: pgcrypto; Type: EXTENSION; Schema: -; Owner: -
--

CREATE EXTENSION IF NOT EXISTS "pgcrypto" WITH SCHEMA "public";


SET default_tablespace = '';

SET default_table_access_method = "heap";

--
-- Name: ai_conversations; Type: TABLE; Schema: public; Owner: -
--

CREATE TABLE "public"."ai_conversations" (
    "id" "uuid" DEFAULT "gen_random_uuid"() NOT NULL,
    "user_id" "uuid",
    "title" character varying(200) DEFAULT ''::character varying,
    "created_at" timestamp with time zone DEFAULT "now"(),
    "updated_at" timestamp with time zone DEFAULT "now"()
);


--
-- Name: ai_messages; Type: TABLE; Schema: public; Owner: -
--

CREATE TABLE "public"."ai_messages" (
    "id" "uuid" DEFAULT "gen_random_uuid"() NOT NULL,
    "conversation_id" "uuid",
    "role" character varying(20) NOT NULL,
    "content" "text" DEFAULT ''::"text" NOT NULL,
    "tool_calls" "jsonb",
    "tool_call_id" character varying(100),
    "tool_name" character varying(100),
    "created_at" timestamp with time zone DEFAULT "now"()
);


--
-- Name: auth_challenges; Type: TABLE; Schema: public; Owner: -
--

CREATE TABLE "public"."auth_challenges" (
    "public_key" "text" NOT NULL,
    "challenge" character varying(128) NOT NULL,
    "created_at" timestamp with time zone DEFAULT "now"()
);


--
-- Name: calendar_event_exceptions; Type: TABLE; Schema: public; Owner: -
--

CREATE TABLE "public"."calendar_event_exceptions" (
    "id" "uuid" DEFAULT "gen_random_uuid"() NOT NULL,
    "event_id" "uuid" NOT NULL,
    "original_date" timestamp with time zone NOT NULL,
    "is_deleted" boolean DEFAULT false,
    "title" character varying(255),
    "description" "text",
    "location" "text",
    "color" character varying(20),
    "start_time" timestamp with time zone,
    "end_time" timestamp with time zone,
    "all_day" boolean,
    "created_at" timestamp with time zone DEFAULT "now"()
);


--
-- Name: calendar_event_rsvps; Type: TABLE; Schema: public; Owner: -
--

CREATE TABLE "public"."calendar_event_rsvps" (
    "event_id" "uuid" NOT NULL,
    "user_id" "uuid" NOT NULL,
    "occurrence_date" timestamp with time zone DEFAULT '1970-01-01 00:00:00+00'::timestamp with time zone NOT NULL,
    "status" character varying(10) NOT NULL,
    "responded_at" timestamp with time zone DEFAULT "now"()
);


--
-- Name: calendar_events; Type: TABLE; Schema: public; Owner: -
--

CREATE TABLE "public"."calendar_events" (
    "id" "uuid" DEFAULT "gen_random_uuid"() NOT NULL,
    "space_id" "uuid" NOT NULL,
    "title" character varying(255) NOT NULL,
    "description" "text" DEFAULT ''::"text",
    "location" "text" DEFAULT ''::"text",
    "color" character varying(20) DEFAULT 'blue'::character varying,
    "start_time" timestamp with time zone NOT NULL,
    "end_time" timestamp with time zone NOT NULL,
    "all_day" boolean DEFAULT false,
    "rrule" "text" DEFAULT ''::"text",
    "created_by" "uuid" NOT NULL,
    "created_at" timestamp with time zone DEFAULT "now"(),
    "updated_at" timestamp with time zone DEFAULT "now"()
);


--
-- Name: calendar_permissions; Type: TABLE; Schema: public; Owner: -
--

CREATE TABLE "public"."calendar_permissions" (
    "id" "uuid" DEFAULT "gen_random_uuid"() NOT NULL,
    "space_id" "uuid" NOT NULL,
    "user_id" "uuid" NOT NULL,
    "permission" character varying(20) NOT NULL,
    "granted_by" "uuid" NOT NULL,
    "created_at" timestamp with time zone DEFAULT "now"()
);


--
-- Name: channel_members; Type: TABLE; Schema: public; Owner: -
--

CREATE TABLE "public"."channel_members" (
    "channel_id" "uuid" NOT NULL,
    "user_id" "uuid" NOT NULL,
    "joined_at" timestamp with time zone DEFAULT "now"(),
    "role" character varying(20) DEFAULT 'write'::character varying
);


--
-- Name: channel_read_state; Type: TABLE; Schema: public; Owner: -
--

CREATE TABLE "public"."channel_read_state" (
    "channel_id" "uuid" NOT NULL,
    "user_id" "uuid" NOT NULL,
    "last_read_at" timestamp with time zone DEFAULT "now"() NOT NULL,
    "last_read_message_id" "uuid"
);


--
-- Name: channels; Type: TABLE; Schema: public; Owner: -
--

CREATE TABLE "public"."channels" (
    "id" "uuid" DEFAULT "gen_random_uuid"() NOT NULL,
    "name" character varying(100),
    "description" "text",
    "is_direct" boolean DEFAULT false,
    "created_by" "uuid",
    "created_at" timestamp with time zone DEFAULT "now"(),
    "is_public" boolean DEFAULT true,
    "default_role" character varying(20) DEFAULT 'write'::character varying,
    "space_id" "uuid",
    "conversation_name" character varying(200),
    "is_archived" boolean DEFAULT false,
    "default_join" boolean DEFAULT false
);


--
-- Name: device_tokens; Type: TABLE; Schema: public; Owner: -
--

CREATE TABLE "public"."device_tokens" (
    "id" "uuid" DEFAULT "gen_random_uuid"() NOT NULL,
    "user_id" "uuid" NOT NULL,
    "token" character varying(64) NOT NULL,
    "expires_at" timestamp with time zone NOT NULL,
    "used" boolean DEFAULT false,
    "created_at" timestamp with time zone DEFAULT "now"()
);


--
-- Name: invite_tokens; Type: TABLE; Schema: public; Owner: -
--

CREATE TABLE "public"."invite_tokens" (
    "id" "uuid" DEFAULT "gen_random_uuid"() NOT NULL,
    "token" character varying(64) NOT NULL,
    "created_by" "uuid",
    "used_by" "uuid",
    "expires_at" timestamp with time zone NOT NULL,
    "created_at" timestamp with time zone DEFAULT "now"(),
    "used_at" timestamp with time zone,
    "max_uses" integer DEFAULT 1 NOT NULL,
    "use_count" integer DEFAULT 0 NOT NULL
);


--
-- Name: invite_uses; Type: TABLE; Schema: public; Owner: -
--

CREATE TABLE "public"."invite_uses" (
    "id" "uuid" DEFAULT "gen_random_uuid"() NOT NULL,
    "invite_id" "uuid" NOT NULL,
    "used_by" "uuid" NOT NULL,
    "used_at" timestamp with time zone DEFAULT "now"()
);


--
-- Name: join_requests; Type: TABLE; Schema: public; Owner: -
--

CREATE TABLE "public"."join_requests" (
    "id" "uuid" DEFAULT "gen_random_uuid"() NOT NULL,
    "username" character varying(50) NOT NULL,
    "display_name" character varying(100) NOT NULL,
    "public_key" "text" DEFAULT ''::"text",
    "status" character varying(20) DEFAULT 'pending'::character varying,
    "reviewed_by" "uuid",
    "created_at" timestamp with time zone DEFAULT "now"(),
    "auth_method" "text" DEFAULT ''::"text",
    "credential_data" "text" DEFAULT ''::"text",
    "session_token" "text" DEFAULT ''::"text"
);


--
-- Name: mentions; Type: TABLE; Schema: public; Owner: -
--

CREATE TABLE "public"."mentions" (
    "id" "uuid" DEFAULT "gen_random_uuid"() NOT NULL,
    "message_id" "uuid",
    "channel_id" "uuid" NOT NULL,
    "mentioned_user_id" "uuid",
    "is_channel_mention" boolean DEFAULT false,
    "created_at" timestamp with time zone DEFAULT "now"()
);


--
-- Name: messages; Type: TABLE; Schema: public; Owner: -
--

CREATE TABLE "public"."messages" (
    "id" "uuid" DEFAULT "gen_random_uuid"() NOT NULL,
    "channel_id" "uuid",
    "user_id" "uuid",
    "content" "text" NOT NULL,
    "created_at" timestamp with time zone DEFAULT "now"(),
    "edited_at" timestamp with time zone,
    "is_deleted" boolean DEFAULT false,
    "file_id" "text",
    "file_name" "text",
    "file_size" bigint,
    "file_type" "text",
    "content_tsv" "tsvector" GENERATED ALWAYS AS ("to_tsvector"('"english"'::"regconfig", COALESCE("content", ''::"text"))) STORED,
    "reply_to_message_id" "uuid",
    "is_ai_assisted" boolean DEFAULT false
);


--
-- Name: mfa_pending_tokens; Type: TABLE; Schema: public; Owner: -
--

CREATE TABLE "public"."mfa_pending_tokens" (
    "id" "uuid" DEFAULT "gen_random_uuid"() NOT NULL,
    "user_id" "uuid" NOT NULL,
    "auth_method" "text" NOT NULL,
    "created_at" timestamp with time zone DEFAULT "now"(),
    "expires_at" timestamp with time zone NOT NULL
);


--
-- Name: notifications; Type: TABLE; Schema: public; Owner: -
--

CREATE TABLE "public"."notifications" (
    "id" "uuid" DEFAULT "gen_random_uuid"() NOT NULL,
    "user_id" "uuid" NOT NULL,
    "type" character varying(50) NOT NULL,
    "source_user_id" "uuid",
    "channel_id" "uuid",
    "message_id" "uuid",
    "content" "text",
    "is_read" boolean DEFAULT false NOT NULL,
    "created_at" timestamp with time zone DEFAULT "now"() NOT NULL
);


--
-- Name: password_credentials; Type: TABLE; Schema: public; Owner: -
--

CREATE TABLE "public"."password_credentials" (
    "id" "uuid" DEFAULT "gen_random_uuid"() NOT NULL,
    "user_id" "uuid" NOT NULL,
    "password_hash" "text" NOT NULL,
    "created_at" timestamp with time zone DEFAULT "now"()
);


--
-- Name: password_history; Type: TABLE; Schema: public; Owner: -
--

CREATE TABLE "public"."password_history" (
    "id" "uuid" DEFAULT "gen_random_uuid"() NOT NULL,
    "user_id" "uuid" NOT NULL,
    "password_hash" "text" NOT NULL,
    "created_at" timestamp with time zone DEFAULT "now"()
);


--
-- Name: pki_credentials; Type: TABLE; Schema: public; Owner: -
--

CREATE TABLE "public"."pki_credentials" (
    "id" "uuid" DEFAULT "gen_random_uuid"() NOT NULL,
    "user_id" "uuid" NOT NULL,
    "public_key" "text" NOT NULL,
    "device_name" character varying(100) DEFAULT 'Browser Key'::character varying,
    "created_at" timestamp with time zone DEFAULT "now"()
);


--
-- Name: reactions; Type: TABLE; Schema: public; Owner: -
--

CREATE TABLE "public"."reactions" (
    "id" "uuid" DEFAULT "gen_random_uuid"() NOT NULL,
    "message_id" "uuid" NOT NULL,
    "user_id" "uuid" NOT NULL,
    "emoji" "text" NOT NULL,
    "created_at" timestamp with time zone DEFAULT "now"()
);


--
-- Name: recovery_keys; Type: TABLE; Schema: public; Owner: -
--

CREATE TABLE "public"."recovery_keys" (
    "id" "uuid" DEFAULT "gen_random_uuid"() NOT NULL,
    "user_id" "uuid" NOT NULL,
    "key_hash" "text" NOT NULL,
    "used" boolean DEFAULT false NOT NULL,
    "created_at" timestamp with time zone DEFAULT "now"()
);


--
-- Name: recovery_tokens; Type: TABLE; Schema: public; Owner: -
--

CREATE TABLE "public"."recovery_tokens" (
    "id" "uuid" DEFAULT "gen_random_uuid"() NOT NULL,
    "token" character varying(64) NOT NULL,
    "created_by" "uuid",
    "for_user_id" "uuid" NOT NULL,
    "used" boolean DEFAULT false,
    "used_at" timestamp with time zone,
    "expires_at" timestamp with time zone NOT NULL,
    "created_at" timestamp with time zone DEFAULT "now"()
);


--
-- Name: server_settings; Type: TABLE; Schema: public; Owner: -
--

CREATE TABLE "public"."server_settings" (
    "key" "text" NOT NULL,
    "value" "text" NOT NULL
);


--
-- Name: sessions; Type: TABLE; Schema: public; Owner: -
--

CREATE TABLE "public"."sessions" (
    "id" "uuid" DEFAULT "gen_random_uuid"() NOT NULL,
    "user_id" "uuid",
    "token" character varying(128) NOT NULL,
    "expires_at" timestamp with time zone NOT NULL,
    "created_at" timestamp with time zone DEFAULT "now"()
);


--
-- Name: space_file_permissions; Type: TABLE; Schema: public; Owner: -
--

CREATE TABLE "public"."space_file_permissions" (
    "id" "uuid" DEFAULT "gen_random_uuid"() NOT NULL,
    "file_id" "uuid" NOT NULL,
    "user_id" "uuid",
    "permission" character varying(20) NOT NULL,
    "granted_by" "uuid" NOT NULL,
    "created_at" timestamp with time zone DEFAULT "now"()
);


--
-- Name: space_file_versions; Type: TABLE; Schema: public; Owner: -
--

CREATE TABLE "public"."space_file_versions" (
    "id" "uuid" DEFAULT "gen_random_uuid"() NOT NULL,
    "file_id" "uuid" NOT NULL,
    "version_number" integer NOT NULL,
    "disk_file_id" "text" NOT NULL,
    "file_size" bigint NOT NULL,
    "mime_type" "text",
    "uploaded_by" "uuid" NOT NULL,
    "created_at" timestamp with time zone DEFAULT "now"()
);


--
-- Name: space_files; Type: TABLE; Schema: public; Owner: -
--

CREATE TABLE "public"."space_files" (
    "id" "uuid" DEFAULT "gen_random_uuid"() NOT NULL,
    "space_id" "uuid" NOT NULL,
    "parent_id" "uuid",
    "name" character varying(255) NOT NULL,
    "is_folder" boolean DEFAULT false NOT NULL,
    "disk_file_id" "text",
    "file_size" bigint DEFAULT 0,
    "mime_type" "text",
    "created_by" "uuid" NOT NULL,
    "created_at" timestamp with time zone DEFAULT "now"(),
    "updated_at" timestamp with time zone DEFAULT "now"(),
    "is_deleted" boolean DEFAULT false,
    "tool_source" character varying(20) DEFAULT 'files'::character varying
);


--
-- Name: space_invites; Type: TABLE; Schema: public; Owner: -
--

CREATE TABLE "public"."space_invites" (
    "id" "uuid" DEFAULT "gen_random_uuid"() NOT NULL,
    "space_id" "uuid" NOT NULL,
    "invited_user_id" "uuid" NOT NULL,
    "invited_by" "uuid" NOT NULL,
    "role" character varying(20) DEFAULT 'user'::character varying,
    "status" character varying(20) DEFAULT 'pending'::character varying,
    "created_at" timestamp with time zone DEFAULT "now"()
);


--
-- Name: space_members; Type: TABLE; Schema: public; Owner: -
--

CREATE TABLE "public"."space_members" (
    "space_id" "uuid" NOT NULL,
    "user_id" "uuid" NOT NULL,
    "role" character varying(20) DEFAULT 'user'::character varying,
    "joined_at" timestamp with time zone DEFAULT "now"()
);


--
-- Name: space_tools; Type: TABLE; Schema: public; Owner: -
--

CREATE TABLE "public"."space_tools" (
    "space_id" "uuid" NOT NULL,
    "tool_name" character varying(50) NOT NULL,
    "enabled_by" "uuid",
    "enabled_at" timestamp with time zone DEFAULT "now"()
);


--
-- Name: spaces; Type: TABLE; Schema: public; Owner: -
--

CREATE TABLE "public"."spaces" (
    "id" "uuid" DEFAULT "gen_random_uuid"() NOT NULL,
    "name" character varying(100) NOT NULL,
    "description" "text",
    "icon" character varying(10) DEFAULT ''::character varying,
    "is_public" boolean DEFAULT true,
    "default_role" character varying(20) DEFAULT 'user'::character varying,
    "created_by" "uuid",
    "created_at" timestamp with time zone DEFAULT "now"(),
    "is_archived" boolean DEFAULT false,
    "avatar_file_id" "text" DEFAULT ''::"text",
    "profile_color" character varying(7) DEFAULT ''::character varying,
    "storage_limit" bigint DEFAULT 0,
    "auto_delete_old_versions" boolean DEFAULT false,
    "is_personal" boolean DEFAULT false,
    "personal_owner_id" "uuid"
);


--
-- Name: task_activity; Type: TABLE; Schema: public; Owner: -
--

CREATE TABLE "public"."task_activity" (
    "id" "uuid" DEFAULT "gen_random_uuid"() NOT NULL,
    "task_id" "uuid" NOT NULL,
    "user_id" "uuid" NOT NULL,
    "action" character varying(50) NOT NULL,
    "details" "text" DEFAULT '{}'::"text",
    "created_at" timestamp with time zone DEFAULT "now"()
);


--
-- Name: task_assignees; Type: TABLE; Schema: public; Owner: -
--

CREATE TABLE "public"."task_assignees" (
    "task_id" "uuid" NOT NULL,
    "user_id" "uuid" NOT NULL
);


--
-- Name: task_board_permissions; Type: TABLE; Schema: public; Owner: -
--

CREATE TABLE "public"."task_board_permissions" (
    "id" "uuid" DEFAULT "gen_random_uuid"() NOT NULL,
    "space_id" "uuid" NOT NULL,
    "user_id" "uuid" NOT NULL,
    "permission" character varying(20) NOT NULL,
    "granted_by" "uuid" NOT NULL,
    "created_at" timestamp with time zone DEFAULT "now"()
);


--
-- Name: task_boards; Type: TABLE; Schema: public; Owner: -
--

CREATE TABLE "public"."task_boards" (
    "id" "uuid" DEFAULT "gen_random_uuid"() NOT NULL,
    "space_id" "uuid" NOT NULL,
    "name" character varying(255) NOT NULL,
    "description" "text" DEFAULT ''::"text",
    "created_by" "uuid" NOT NULL,
    "created_at" timestamp with time zone DEFAULT "now"(),
    "updated_at" timestamp with time zone DEFAULT "now"()
);


--
-- Name: task_checklist_items; Type: TABLE; Schema: public; Owner: -
--

CREATE TABLE "public"."task_checklist_items" (
    "id" "uuid" DEFAULT "gen_random_uuid"() NOT NULL,
    "checklist_id" "uuid" NOT NULL,
    "content" character varying(500) NOT NULL,
    "is_checked" boolean DEFAULT false,
    "position" integer DEFAULT 0 NOT NULL
);


--
-- Name: task_checklists; Type: TABLE; Schema: public; Owner: -
--

CREATE TABLE "public"."task_checklists" (
    "id" "uuid" DEFAULT "gen_random_uuid"() NOT NULL,
    "task_id" "uuid" NOT NULL,
    "title" character varying(255) NOT NULL,
    "position" integer DEFAULT 0 NOT NULL
);


--
-- Name: task_columns; Type: TABLE; Schema: public; Owner: -
--

CREATE TABLE "public"."task_columns" (
    "id" "uuid" DEFAULT "gen_random_uuid"() NOT NULL,
    "board_id" "uuid" NOT NULL,
    "name" character varying(255) NOT NULL,
    "position" integer DEFAULT 0 NOT NULL,
    "wip_limit" integer DEFAULT 0,
    "color" character varying(50) DEFAULT ''::character varying,
    "created_at" timestamp with time zone DEFAULT "now"()
);


--
-- Name: task_dependencies; Type: TABLE; Schema: public; Owner: -
--

CREATE TABLE "public"."task_dependencies" (
    "id" "uuid" DEFAULT "gen_random_uuid"() NOT NULL,
    "task_id" "uuid" NOT NULL,
    "depends_on_id" "uuid" NOT NULL,
    "dependency_type" character varying(30) DEFAULT 'finish_to_start'::character varying,
    "created_at" timestamp with time zone DEFAULT "now"(),
    CONSTRAINT "task_dependencies_check" CHECK (("task_id" <> "depends_on_id"))
);


--
-- Name: task_label_assignments; Type: TABLE; Schema: public; Owner: -
--

CREATE TABLE "public"."task_label_assignments" (
    "task_id" "uuid" NOT NULL,
    "label_id" "uuid" NOT NULL
);


--
-- Name: task_labels; Type: TABLE; Schema: public; Owner: -
--

CREATE TABLE "public"."task_labels" (
    "id" "uuid" DEFAULT "gen_random_uuid"() NOT NULL,
    "board_id" "uuid" NOT NULL,
    "name" character varying(100) NOT NULL,
    "color" character varying(50) DEFAULT ''::character varying
);


--
-- Name: tasks; Type: TABLE; Schema: public; Owner: -
--

CREATE TABLE "public"."tasks" (
    "id" "uuid" DEFAULT "gen_random_uuid"() NOT NULL,
    "board_id" "uuid" NOT NULL,
    "column_id" "uuid" NOT NULL,
    "title" character varying(255) NOT NULL,
    "description" "text" DEFAULT ''::"text",
    "priority" character varying(20) DEFAULT 'medium'::character varying,
    "due_date" timestamp with time zone,
    "color" character varying(50) DEFAULT ''::character varying,
    "position" integer DEFAULT 0 NOT NULL,
    "created_by" "uuid" NOT NULL,
    "created_at" timestamp with time zone DEFAULT "now"(),
    "updated_at" timestamp with time zone DEFAULT "now"(),
    "start_date" timestamp with time zone,
    "duration_days" integer DEFAULT 0
);


--
-- Name: totp_credentials; Type: TABLE; Schema: public; Owner: -
--

CREATE TABLE "public"."totp_credentials" (
    "id" "uuid" DEFAULT "gen_random_uuid"() NOT NULL,
    "user_id" "uuid" NOT NULL,
    "secret" "text" NOT NULL,
    "verified" boolean DEFAULT false,
    "created_at" timestamp with time zone DEFAULT "now"(),
    "totp_last_step" bigint
);


--
-- Name: user_keys; Type: TABLE; Schema: public; Owner: -
--

CREATE TABLE "public"."user_keys" (
    "id" "uuid" DEFAULT "gen_random_uuid"() NOT NULL,
    "user_id" "uuid" NOT NULL,
    "public_key" "text" NOT NULL,
    "device_name" character varying(100) DEFAULT 'Primary Device'::character varying,
    "created_at" timestamp with time zone DEFAULT "now"()
);


--
-- Name: user_settings; Type: TABLE; Schema: public; Owner: -
--

CREATE TABLE "public"."user_settings" (
    "user_id" "uuid" NOT NULL,
    "key" character varying(100) NOT NULL,
    "value" "text" NOT NULL
);


--
-- Name: users; Type: TABLE; Schema: public; Owner: -
--

CREATE TABLE "public"."users" (
    "id" "uuid" DEFAULT "gen_random_uuid"() NOT NULL,
    "username" character varying(50) NOT NULL,
    "display_name" character varying(100) NOT NULL,
    "public_key" "text" DEFAULT ''::"text",
    "role" character varying(20) DEFAULT 'user'::character varying,
    "is_online" boolean DEFAULT false,
    "last_seen" timestamp with time zone,
    "created_at" timestamp with time zone DEFAULT "now"(),
    "bio" "text" DEFAULT ''::"text",
    "status" character varying(100) DEFAULT ''::character varying,
    "avatar_file_id" "text" DEFAULT ''::"text",
    "profile_color" character varying(7) DEFAULT ''::character varying,
    "is_banned" boolean DEFAULT false NOT NULL,
    "banned_at" timestamp with time zone,
    "banned_by" "uuid"
);


--
-- Name: webauthn_challenges; Type: TABLE; Schema: public; Owner: -
--

CREATE TABLE "public"."webauthn_challenges" (
    "id" "uuid" DEFAULT "gen_random_uuid"() NOT NULL,
    "challenge" "text" NOT NULL,
    "extra_data" "text",
    "created_at" timestamp with time zone DEFAULT "now"()
);


--
-- Name: webauthn_credentials; Type: TABLE; Schema: public; Owner: -
--

CREATE TABLE "public"."webauthn_credentials" (
    "id" "uuid" DEFAULT "gen_random_uuid"() NOT NULL,
    "user_id" "uuid" NOT NULL,
    "credential_id" "text" NOT NULL,
    "public_key" "bytea" NOT NULL,
    "sign_count" integer DEFAULT 0 NOT NULL,
    "device_name" character varying(100) DEFAULT 'Passkey'::character varying,
    "transports" "text",
    "created_at" timestamp with time zone DEFAULT "now"()
);


--
-- Name: wiki_page_permissions; Type: TABLE; Schema: public; Owner: -
--

CREATE TABLE "public"."wiki_page_permissions" (
    "id" "uuid" DEFAULT "gen_random_uuid"() NOT NULL,
    "page_id" "uuid" NOT NULL,
    "user_id" "uuid",
    "permission" character varying(20) NOT NULL,
    "granted_by" "uuid" NOT NULL,
    "created_at" timestamp with time zone DEFAULT "now"()
);


--
-- Name: wiki_page_versions; Type: TABLE; Schema: public; Owner: -
--

CREATE TABLE "public"."wiki_page_versions" (
    "id" "uuid" DEFAULT "gen_random_uuid"() NOT NULL,
    "page_id" "uuid" NOT NULL,
    "version_number" integer NOT NULL,
    "title" character varying(500) NOT NULL,
    "content" "text" DEFAULT ''::"text" NOT NULL,
    "content_text" "text" DEFAULT ''::"text",
    "is_major" boolean DEFAULT false,
    "edited_by" "uuid" NOT NULL,
    "created_at" timestamp with time zone DEFAULT "now"()
);


--
-- Name: wiki_pages; Type: TABLE; Schema: public; Owner: -
--

CREATE TABLE "public"."wiki_pages" (
    "id" "uuid" DEFAULT "gen_random_uuid"() NOT NULL,
    "space_id" "uuid" NOT NULL,
    "parent_id" "uuid",
    "title" character varying(500) NOT NULL,
    "slug" character varying(500) NOT NULL,
    "is_folder" boolean DEFAULT false NOT NULL,
    "content" "text" DEFAULT ''::"text",
    "content_text" "text" DEFAULT ''::"text",
    "content_tsv" "tsvector",
    "icon" character varying(50) DEFAULT ''::character varying,
    "cover_image_file_id" "text" DEFAULT ''::"text",
    "position" integer DEFAULT 0,
    "is_deleted" boolean DEFAULT false,
    "created_by" "uuid" NOT NULL,
    "created_at" timestamp with time zone DEFAULT "now"(),
    "updated_at" timestamp with time zone DEFAULT "now"(),
    "last_edited_by" "uuid"
);


--
-- Name: wiki_permissions; Type: TABLE; Schema: public; Owner: -
--

CREATE TABLE "public"."wiki_permissions" (
    "id" "uuid" DEFAULT "gen_random_uuid"() NOT NULL,
    "space_id" "uuid" NOT NULL,
    "user_id" "uuid",
    "permission" character varying(20) NOT NULL,
    "granted_by" "uuid" NOT NULL,
    "created_at" timestamp with time zone DEFAULT "now"()
);


--
-- Name: ai_conversations ai_conversations_pkey; Type: CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY "public"."ai_conversations"
    ADD CONSTRAINT "ai_conversations_pkey" PRIMARY KEY ("id");


--
-- Name: ai_messages ai_messages_pkey; Type: CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY "public"."ai_messages"
    ADD CONSTRAINT "ai_messages_pkey" PRIMARY KEY ("id");


--
-- Name: auth_challenges auth_challenges_pkey; Type: CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY "public"."auth_challenges"
    ADD CONSTRAINT "auth_challenges_pkey" PRIMARY KEY ("public_key");


--
-- Name: calendar_event_exceptions calendar_event_exceptions_event_id_original_date_key; Type: CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY "public"."calendar_event_exceptions"
    ADD CONSTRAINT "calendar_event_exceptions_event_id_original_date_key" UNIQUE ("event_id", "original_date");


--
-- Name: calendar_event_exceptions calendar_event_exceptions_pkey; Type: CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY "public"."calendar_event_exceptions"
    ADD CONSTRAINT "calendar_event_exceptions_pkey" PRIMARY KEY ("id");


--
-- Name: calendar_event_rsvps calendar_event_rsvps_pkey; Type: CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY "public"."calendar_event_rsvps"
    ADD CONSTRAINT "calendar_event_rsvps_pkey" PRIMARY KEY ("event_id", "user_id", "occurrence_date");


--
-- Name: calendar_events calendar_events_pkey; Type: CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY "public"."calendar_events"
    ADD CONSTRAINT "calendar_events_pkey" PRIMARY KEY ("id");


--
-- Name: calendar_permissions calendar_permissions_pkey; Type: CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY "public"."calendar_permissions"
    ADD CONSTRAINT "calendar_permissions_pkey" PRIMARY KEY ("id");


--
-- Name: calendar_permissions calendar_permissions_space_id_user_id_key; Type: CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY "public"."calendar_permissions"
    ADD CONSTRAINT "calendar_permissions_space_id_user_id_key" UNIQUE ("space_id", "user_id");


--
-- Name: channel_members channel_members_pkey; Type: CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY "public"."channel_members"
    ADD CONSTRAINT "channel_members_pkey" PRIMARY KEY ("channel_id", "user_id");


--
-- Name: channel_read_state channel_read_state_pkey; Type: CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY "public"."channel_read_state"
    ADD CONSTRAINT "channel_read_state_pkey" PRIMARY KEY ("channel_id", "user_id");


--
-- Name: channels channels_pkey; Type: CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY "public"."channels"
    ADD CONSTRAINT "channels_pkey" PRIMARY KEY ("id");


--
-- Name: device_tokens device_tokens_pkey; Type: CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY "public"."device_tokens"
    ADD CONSTRAINT "device_tokens_pkey" PRIMARY KEY ("id");


--
-- Name: device_tokens device_tokens_token_key; Type: CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY "public"."device_tokens"
    ADD CONSTRAINT "device_tokens_token_key" UNIQUE ("token");


--
-- Name: invite_tokens invite_tokens_pkey; Type: CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY "public"."invite_tokens"
    ADD CONSTRAINT "invite_tokens_pkey" PRIMARY KEY ("id");


--
-- Name: invite_tokens invite_tokens_token_key; Type: CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY "public"."invite_tokens"
    ADD CONSTRAINT "invite_tokens_token_key" UNIQUE ("token");


--
-- Name: invite_uses invite_uses_pkey; Type: CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY "public"."invite_uses"
    ADD CONSTRAINT "invite_uses_pkey" PRIMARY KEY ("id");


--
-- Name: join_requests join_requests_pkey; Type: CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY "public"."join_requests"
    ADD CONSTRAINT "join_requests_pkey" PRIMARY KEY ("id");


--
-- Name: mentions mentions_pkey; Type: CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY "public"."mentions"
    ADD CONSTRAINT "mentions_pkey" PRIMARY KEY ("id");


--
-- Name: messages messages_pkey; Type: CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY "public"."messages"
    ADD CONSTRAINT "messages_pkey" PRIMARY KEY ("id");


--
-- Name: mfa_pending_tokens mfa_pending_tokens_pkey; Type: CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY "public"."mfa_pending_tokens"
    ADD CONSTRAINT "mfa_pending_tokens_pkey" PRIMARY KEY ("id");


--
-- Name: notifications notifications_pkey; Type: CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY "public"."notifications"
    ADD CONSTRAINT "notifications_pkey" PRIMARY KEY ("id");


--
-- Name: password_credentials password_credentials_pkey; Type: CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY "public"."password_credentials"
    ADD CONSTRAINT "password_credentials_pkey" PRIMARY KEY ("id");


--
-- Name: password_history password_history_pkey; Type: CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY "public"."password_history"
    ADD CONSTRAINT "password_history_pkey" PRIMARY KEY ("id");


--
-- Name: pki_credentials pki_credentials_pkey; Type: CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY "public"."pki_credentials"
    ADD CONSTRAINT "pki_credentials_pkey" PRIMARY KEY ("id");


--
-- Name: reactions reactions_message_id_user_id_emoji_key; Type: CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY "public"."reactions"
    ADD CONSTRAINT "reactions_message_id_user_id_emoji_key" UNIQUE ("message_id", "user_id", "emoji");


--
-- Name: reactions reactions_pkey; Type: CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY "public"."reactions"
    ADD CONSTRAINT "reactions_pkey" PRIMARY KEY ("id");


--
-- Name: recovery_keys recovery_keys_pkey; Type: CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY "public"."recovery_keys"
    ADD CONSTRAINT "recovery_keys_pkey" PRIMARY KEY ("id");


--
-- Name: recovery_tokens recovery_tokens_pkey; Type: CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY "public"."recovery_tokens"
    ADD CONSTRAINT "recovery_tokens_pkey" PRIMARY KEY ("id");


--
-- Name: recovery_tokens recovery_tokens_token_key; Type: CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY "public"."recovery_tokens"
    ADD CONSTRAINT "recovery_tokens_token_key" UNIQUE ("token");


--
-- Name: server_settings server_settings_pkey; Type: CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY "public"."server_settings"
    ADD CONSTRAINT "server_settings_pkey" PRIMARY KEY ("key");


--
-- Name: sessions sessions_pkey; Type: CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY "public"."sessions"
    ADD CONSTRAINT "sessions_pkey" PRIMARY KEY ("id");


--
-- Name: sessions sessions_token_key; Type: CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY "public"."sessions"
    ADD CONSTRAINT "sessions_token_key" UNIQUE ("token");


--
-- Name: space_file_permissions space_file_permissions_file_id_user_id_key; Type: CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY "public"."space_file_permissions"
    ADD CONSTRAINT "space_file_permissions_file_id_user_id_key" UNIQUE ("file_id", "user_id");


--
-- Name: space_file_permissions space_file_permissions_pkey; Type: CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY "public"."space_file_permissions"
    ADD CONSTRAINT "space_file_permissions_pkey" PRIMARY KEY ("id");


--
-- Name: space_file_versions space_file_versions_file_id_version_number_key; Type: CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY "public"."space_file_versions"
    ADD CONSTRAINT "space_file_versions_file_id_version_number_key" UNIQUE ("file_id", "version_number");


--
-- Name: space_file_versions space_file_versions_pkey; Type: CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY "public"."space_file_versions"
    ADD CONSTRAINT "space_file_versions_pkey" PRIMARY KEY ("id");


--
-- Name: space_files space_files_pkey; Type: CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY "public"."space_files"
    ADD CONSTRAINT "space_files_pkey" PRIMARY KEY ("id");


--
-- Name: space_invites space_invites_pkey; Type: CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY "public"."space_invites"
    ADD CONSTRAINT "space_invites_pkey" PRIMARY KEY ("id");


--
-- Name: space_members space_members_pkey; Type: CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY "public"."space_members"
    ADD CONSTRAINT "space_members_pkey" PRIMARY KEY ("space_id", "user_id");


--
-- Name: space_tools space_tools_pkey; Type: CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY "public"."space_tools"
    ADD CONSTRAINT "space_tools_pkey" PRIMARY KEY ("space_id", "tool_name");


--
-- Name: spaces spaces_pkey; Type: CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY "public"."spaces"
    ADD CONSTRAINT "spaces_pkey" PRIMARY KEY ("id");


--
-- Name: task_activity task_activity_pkey; Type: CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY "public"."task_activity"
    ADD CONSTRAINT "task_activity_pkey" PRIMARY KEY ("id");


--
-- Name: task_assignees task_assignees_pkey; Type: CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY "public"."task_assignees"
    ADD CONSTRAINT "task_assignees_pkey" PRIMARY KEY ("task_id", "user_id");


--
-- Name: task_board_permissions task_board_permissions_pkey; Type: CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY "public"."task_board_permissions"
    ADD CONSTRAINT "task_board_permissions_pkey" PRIMARY KEY ("id");


--
-- Name: task_board_permissions task_board_permissions_space_id_user_id_key; Type: CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY "public"."task_board_permissions"
    ADD CONSTRAINT "task_board_permissions_space_id_user_id_key" UNIQUE ("space_id", "user_id");


--
-- Name: task_boards task_boards_pkey; Type: CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY "public"."task_boards"
    ADD CONSTRAINT "task_boards_pkey" PRIMARY KEY ("id");


--
-- Name: task_checklist_items task_checklist_items_pkey; Type: CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY "public"."task_checklist_items"
    ADD CONSTRAINT "task_checklist_items_pkey" PRIMARY KEY ("id");


--
-- Name: task_checklists task_checklists_pkey; Type: CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY "public"."task_checklists"
    ADD CONSTRAINT "task_checklists_pkey" PRIMARY KEY ("id");


--
-- Name: task_columns task_columns_pkey; Type: CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY "public"."task_columns"
    ADD CONSTRAINT "task_columns_pkey" PRIMARY KEY ("id");


--
-- Name: task_dependencies task_dependencies_pkey; Type: CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY "public"."task_dependencies"
    ADD CONSTRAINT "task_dependencies_pkey" PRIMARY KEY ("id");


--
-- Name: task_dependencies task_dependencies_task_id_depends_on_id_key; Type: CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY "public"."task_dependencies"
    ADD CONSTRAINT "task_dependencies_task_id_depends_on_id_key" UNIQUE ("task_id", "depends_on_id");


--
-- Name: task_label_assignments task_label_assignments_pkey; Type: CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY "public"."task_label_assignments"
    ADD CONSTRAINT "task_label_assignments_pkey" PRIMARY KEY ("task_id", "label_id");


--
-- Name: task_labels task_labels_pkey; Type: CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY "public"."task_labels"
    ADD CONSTRAINT "task_labels_pkey" PRIMARY KEY ("id");


--
-- Name: tasks tasks_pkey; Type: CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY "public"."tasks"
    ADD CONSTRAINT "tasks_pkey" PRIMARY KEY ("id");


--
-- Name: totp_credentials totp_credentials_pkey; Type: CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY "public"."totp_credentials"
    ADD CONSTRAINT "totp_credentials_pkey" PRIMARY KEY ("id");


--
-- Name: totp_credentials totp_credentials_user_id_key; Type: CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY "public"."totp_credentials"
    ADD CONSTRAINT "totp_credentials_user_id_key" UNIQUE ("user_id");


--
-- Name: user_keys user_keys_pkey; Type: CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY "public"."user_keys"
    ADD CONSTRAINT "user_keys_pkey" PRIMARY KEY ("id");


--
-- Name: user_keys user_keys_public_key_key; Type: CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY "public"."user_keys"
    ADD CONSTRAINT "user_keys_public_key_key" UNIQUE ("public_key");


--
-- Name: user_settings user_settings_pkey; Type: CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY "public"."user_settings"
    ADD CONSTRAINT "user_settings_pkey" PRIMARY KEY ("user_id", "key");


--
-- Name: users users_pkey; Type: CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY "public"."users"
    ADD CONSTRAINT "users_pkey" PRIMARY KEY ("id");


--
-- Name: users users_username_key; Type: CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY "public"."users"
    ADD CONSTRAINT "users_username_key" UNIQUE ("username");


--
-- Name: webauthn_challenges webauthn_challenges_challenge_key; Type: CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY "public"."webauthn_challenges"
    ADD CONSTRAINT "webauthn_challenges_challenge_key" UNIQUE ("challenge");


--
-- Name: webauthn_challenges webauthn_challenges_pkey; Type: CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY "public"."webauthn_challenges"
    ADD CONSTRAINT "webauthn_challenges_pkey" PRIMARY KEY ("id");


--
-- Name: webauthn_credentials webauthn_credentials_credential_id_key; Type: CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY "public"."webauthn_credentials"
    ADD CONSTRAINT "webauthn_credentials_credential_id_key" UNIQUE ("credential_id");


--
-- Name: webauthn_credentials webauthn_credentials_pkey; Type: CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY "public"."webauthn_credentials"
    ADD CONSTRAINT "webauthn_credentials_pkey" PRIMARY KEY ("id");


--
-- Name: wiki_page_permissions wiki_page_permissions_page_id_user_id_key; Type: CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY "public"."wiki_page_permissions"
    ADD CONSTRAINT "wiki_page_permissions_page_id_user_id_key" UNIQUE ("page_id", "user_id");


--
-- Name: wiki_page_permissions wiki_page_permissions_pkey; Type: CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY "public"."wiki_page_permissions"
    ADD CONSTRAINT "wiki_page_permissions_pkey" PRIMARY KEY ("id");


--
-- Name: wiki_page_versions wiki_page_versions_page_id_version_number_key; Type: CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY "public"."wiki_page_versions"
    ADD CONSTRAINT "wiki_page_versions_page_id_version_number_key" UNIQUE ("page_id", "version_number");


--
-- Name: wiki_page_versions wiki_page_versions_pkey; Type: CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY "public"."wiki_page_versions"
    ADD CONSTRAINT "wiki_page_versions_pkey" PRIMARY KEY ("id");


--
-- Name: wiki_pages wiki_pages_pkey; Type: CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY "public"."wiki_pages"
    ADD CONSTRAINT "wiki_pages_pkey" PRIMARY KEY ("id");


--
-- Name: wiki_permissions wiki_permissions_pkey; Type: CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY "public"."wiki_permissions"
    ADD CONSTRAINT "wiki_permissions_pkey" PRIMARY KEY ("id");


--
-- Name: wiki_permissions wiki_permissions_space_id_user_id_key; Type: CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY "public"."wiki_permissions"
    ADD CONSTRAINT "wiki_permissions_space_id_user_id_key" UNIQUE ("space_id", "user_id");


--
-- Name: idx_ai_conversations_user; Type: INDEX; Schema: public; Owner: -
--

CREATE INDEX "idx_ai_conversations_user" ON "public"."ai_conversations" USING "btree" ("user_id", "updated_at" DESC);


--
-- Name: idx_ai_messages_conversation; Type: INDEX; Schema: public; Owner: -
--

CREATE INDEX "idx_ai_messages_conversation" ON "public"."ai_messages" USING "btree" ("conversation_id", "created_at");


--
-- Name: idx_calendar_events_space; Type: INDEX; Schema: public; Owner: -
--

CREATE INDEX "idx_calendar_events_space" ON "public"."calendar_events" USING "btree" ("space_id");


--
-- Name: idx_calendar_events_time; Type: INDEX; Schema: public; Owner: -
--

CREATE INDEX "idx_calendar_events_time" ON "public"."calendar_events" USING "btree" ("space_id", "start_time", "end_time");


--
-- Name: idx_channels_space; Type: INDEX; Schema: public; Owner: -
--

CREATE INDEX "idx_channels_space" ON "public"."channels" USING "btree" ("space_id");


--
-- Name: idx_mentions_user; Type: INDEX; Schema: public; Owner: -
--

CREATE INDEX "idx_mentions_user" ON "public"."mentions" USING "btree" ("mentioned_user_id");


--
-- Name: idx_messages_channel_time; Type: INDEX; Schema: public; Owner: -
--

CREATE INDEX "idx_messages_channel_time" ON "public"."messages" USING "btree" ("channel_id", "created_at");


--
-- Name: idx_messages_fts; Type: INDEX; Schema: public; Owner: -
--

CREATE INDEX "idx_messages_fts" ON "public"."messages" USING "gin" ("content_tsv");


--
-- Name: idx_mfa_pending_user; Type: INDEX; Schema: public; Owner: -
--

CREATE INDEX "idx_mfa_pending_user" ON "public"."mfa_pending_tokens" USING "btree" ("user_id");


--
-- Name: idx_notifications_unread; Type: INDEX; Schema: public; Owner: -
--

CREATE INDEX "idx_notifications_unread" ON "public"."notifications" USING "btree" ("user_id") WHERE ("is_read" = false);


--
-- Name: idx_notifications_user; Type: INDEX; Schema: public; Owner: -
--

CREATE INDEX "idx_notifications_user" ON "public"."notifications" USING "btree" ("user_id", "created_at" DESC);


--
-- Name: idx_password_credentials_user; Type: INDEX; Schema: public; Owner: -
--

CREATE INDEX "idx_password_credentials_user" ON "public"."password_credentials" USING "btree" ("user_id");


--
-- Name: idx_password_history_user; Type: INDEX; Schema: public; Owner: -
--

CREATE INDEX "idx_password_history_user" ON "public"."password_history" USING "btree" ("user_id");


--
-- Name: idx_pki_pubkey; Type: INDEX; Schema: public; Owner: -
--

CREATE INDEX "idx_pki_pubkey" ON "public"."pki_credentials" USING "btree" ("public_key");


--
-- Name: idx_pki_user; Type: INDEX; Schema: public; Owner: -
--

CREATE INDEX "idx_pki_user" ON "public"."pki_credentials" USING "btree" ("user_id");


--
-- Name: idx_reactions_message; Type: INDEX; Schema: public; Owner: -
--

CREATE INDEX "idx_reactions_message" ON "public"."reactions" USING "btree" ("message_id");


--
-- Name: idx_recovery_user; Type: INDEX; Schema: public; Owner: -
--

CREATE INDEX "idx_recovery_user" ON "public"."recovery_keys" USING "btree" ("user_id");


--
-- Name: idx_sessions_expires_at; Type: INDEX; Schema: public; Owner: -
--

CREATE INDEX "idx_sessions_expires_at" ON "public"."sessions" USING "btree" ("expires_at");


--
-- Name: idx_space_file_perms_file; Type: INDEX; Schema: public; Owner: -
--

CREATE INDEX "idx_space_file_perms_file" ON "public"."space_file_permissions" USING "btree" ("file_id");


--
-- Name: idx_space_file_perms_user; Type: INDEX; Schema: public; Owner: -
--

CREATE INDEX "idx_space_file_perms_user" ON "public"."space_file_permissions" USING "btree" ("user_id");


--
-- Name: idx_space_file_versions_file; Type: INDEX; Schema: public; Owner: -
--

CREATE INDEX "idx_space_file_versions_file" ON "public"."space_file_versions" USING "btree" ("file_id");


--
-- Name: idx_space_files_parent; Type: INDEX; Schema: public; Owner: -
--

CREATE INDEX "idx_space_files_parent" ON "public"."space_files" USING "btree" ("parent_id");


--
-- Name: idx_space_files_space; Type: INDEX; Schema: public; Owner: -
--

CREATE INDEX "idx_space_files_space" ON "public"."space_files" USING "btree" ("space_id");


--
-- Name: idx_space_files_space_parent; Type: INDEX; Schema: public; Owner: -
--

CREATE INDEX "idx_space_files_space_parent" ON "public"."space_files" USING "btree" ("space_id", "parent_id") WHERE ("is_deleted" = false);


--
-- Name: idx_space_files_unique_name; Type: INDEX; Schema: public; Owner: -
--

CREATE UNIQUE INDEX "idx_space_files_unique_name" ON "public"."space_files" USING "btree" ("space_id", COALESCE("parent_id", '00000000-0000-0000-0000-000000000000'::"uuid"), "lower"(("name")::"text")) WHERE ("is_deleted" = false);


--
-- Name: idx_space_invites_user; Type: INDEX; Schema: public; Owner: -
--

CREATE INDEX "idx_space_invites_user" ON "public"."space_invites" USING "btree" ("invited_user_id", "status");


--
-- Name: idx_spaces_personal_owner; Type: INDEX; Schema: public; Owner: -
--

CREATE UNIQUE INDEX "idx_spaces_personal_owner" ON "public"."spaces" USING "btree" ("personal_owner_id") WHERE ("is_personal" = true);


--
-- Name: idx_task_activity_task; Type: INDEX; Schema: public; Owner: -
--

CREATE INDEX "idx_task_activity_task" ON "public"."task_activity" USING "btree" ("task_id");


--
-- Name: idx_task_boards_space; Type: INDEX; Schema: public; Owner: -
--

CREATE INDEX "idx_task_boards_space" ON "public"."task_boards" USING "btree" ("space_id");


--
-- Name: idx_task_checklist_items_checklist; Type: INDEX; Schema: public; Owner: -
--

CREATE INDEX "idx_task_checklist_items_checklist" ON "public"."task_checklist_items" USING "btree" ("checklist_id");


--
-- Name: idx_task_checklists_task; Type: INDEX; Schema: public; Owner: -
--

CREATE INDEX "idx_task_checklists_task" ON "public"."task_checklists" USING "btree" ("task_id");


--
-- Name: idx_task_columns_board; Type: INDEX; Schema: public; Owner: -
--

CREATE INDEX "idx_task_columns_board" ON "public"."task_columns" USING "btree" ("board_id");


--
-- Name: idx_task_deps_dep; Type: INDEX; Schema: public; Owner: -
--

CREATE INDEX "idx_task_deps_dep" ON "public"."task_dependencies" USING "btree" ("depends_on_id");


--
-- Name: idx_task_deps_task; Type: INDEX; Schema: public; Owner: -
--

CREATE INDEX "idx_task_deps_task" ON "public"."task_dependencies" USING "btree" ("task_id");


--
-- Name: idx_task_labels_board; Type: INDEX; Schema: public; Owner: -
--

CREATE INDEX "idx_task_labels_board" ON "public"."task_labels" USING "btree" ("board_id");


--
-- Name: idx_tasks_board; Type: INDEX; Schema: public; Owner: -
--

CREATE INDEX "idx_tasks_board" ON "public"."tasks" USING "btree" ("board_id");


--
-- Name: idx_tasks_column; Type: INDEX; Schema: public; Owner: -
--

CREATE INDEX "idx_tasks_column" ON "public"."tasks" USING "btree" ("column_id");


--
-- Name: idx_user_keys_public_key; Type: INDEX; Schema: public; Owner: -
--

CREATE INDEX "idx_user_keys_public_key" ON "public"."user_keys" USING "btree" ("public_key");


--
-- Name: idx_webauthn_cred_id; Type: INDEX; Schema: public; Owner: -
--

CREATE INDEX "idx_webauthn_cred_id" ON "public"."webauthn_credentials" USING "btree" ("credential_id");


--
-- Name: idx_wiki_pages_parent; Type: INDEX; Schema: public; Owner: -
--

CREATE INDEX "idx_wiki_pages_parent" ON "public"."wiki_pages" USING "btree" ("parent_id");


--
-- Name: idx_wiki_pages_space; Type: INDEX; Schema: public; Owner: -
--

CREATE INDEX "idx_wiki_pages_space" ON "public"."wiki_pages" USING "btree" ("space_id");


--
-- Name: idx_wiki_pages_space_parent; Type: INDEX; Schema: public; Owner: -
--

CREATE INDEX "idx_wiki_pages_space_parent" ON "public"."wiki_pages" USING "btree" ("space_id", "parent_id") WHERE ("is_deleted" = false);


--
-- Name: idx_wiki_pages_tsv; Type: INDEX; Schema: public; Owner: -
--

CREATE INDEX "idx_wiki_pages_tsv" ON "public"."wiki_pages" USING "gin" ("content_tsv");


--
-- Name: idx_wiki_pages_unique_slug; Type: INDEX; Schema: public; Owner: -
--

CREATE UNIQUE INDEX "idx_wiki_pages_unique_slug" ON "public"."wiki_pages" USING "btree" ("space_id", COALESCE("parent_id", '00000000-0000-0000-0000-000000000000'::"uuid"), "lower"(("slug")::"text")) WHERE ("is_deleted" = false);


--
-- Name: idx_wiki_permissions_space; Type: INDEX; Schema: public; Owner: -
--

CREATE INDEX "idx_wiki_permissions_space" ON "public"."wiki_permissions" USING "btree" ("space_id");


--
-- Name: idx_wiki_perms_page; Type: INDEX; Schema: public; Owner: -
--

CREATE INDEX "idx_wiki_perms_page" ON "public"."wiki_page_permissions" USING "btree" ("page_id");


--
-- Name: idx_wiki_perms_user; Type: INDEX; Schema: public; Owner: -
--

CREATE INDEX "idx_wiki_perms_user" ON "public"."wiki_page_permissions" USING "btree" ("user_id");


--
-- Name: idx_wiki_versions_page; Type: INDEX; Schema: public; Owner: -
--

CREATE INDEX "idx_wiki_versions_page" ON "public"."wiki_page_versions" USING "btree" ("page_id");


--
-- Name: ai_conversations ai_conversations_user_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY "public"."ai_conversations"
    ADD CONSTRAINT "ai_conversations_user_id_fkey" FOREIGN KEY ("user_id") REFERENCES "public"."users"("id") ON DELETE CASCADE;


--
-- Name: ai_messages ai_messages_conversation_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY "public"."ai_messages"
    ADD CONSTRAINT "ai_messages_conversation_id_fkey" FOREIGN KEY ("conversation_id") REFERENCES "public"."ai_conversations"("id") ON DELETE CASCADE;


--
-- Name: calendar_event_exceptions calendar_event_exceptions_event_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY "public"."calendar_event_exceptions"
    ADD CONSTRAINT "calendar_event_exceptions_event_id_fkey" FOREIGN KEY ("event_id") REFERENCES "public"."calendar_events"("id") ON DELETE CASCADE;


--
-- Name: calendar_event_rsvps calendar_event_rsvps_event_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY "public"."calendar_event_rsvps"
    ADD CONSTRAINT "calendar_event_rsvps_event_id_fkey" FOREIGN KEY ("event_id") REFERENCES "public"."calendar_events"("id") ON DELETE CASCADE;


--
-- Name: calendar_event_rsvps calendar_event_rsvps_user_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY "public"."calendar_event_rsvps"
    ADD CONSTRAINT "calendar_event_rsvps_user_id_fkey" FOREIGN KEY ("user_id") REFERENCES "public"."users"("id") ON DELETE CASCADE;


--
-- Name: calendar_events calendar_events_created_by_fkey; Type: FK CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY "public"."calendar_events"
    ADD CONSTRAINT "calendar_events_created_by_fkey" FOREIGN KEY ("created_by") REFERENCES "public"."users"("id");


--
-- Name: calendar_events calendar_events_space_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY "public"."calendar_events"
    ADD CONSTRAINT "calendar_events_space_id_fkey" FOREIGN KEY ("space_id") REFERENCES "public"."spaces"("id") ON DELETE CASCADE;


--
-- Name: calendar_permissions calendar_permissions_granted_by_fkey; Type: FK CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY "public"."calendar_permissions"
    ADD CONSTRAINT "calendar_permissions_granted_by_fkey" FOREIGN KEY ("granted_by") REFERENCES "public"."users"("id");


--
-- Name: calendar_permissions calendar_permissions_space_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY "public"."calendar_permissions"
    ADD CONSTRAINT "calendar_permissions_space_id_fkey" FOREIGN KEY ("space_id") REFERENCES "public"."spaces"("id") ON DELETE CASCADE;


--
-- Name: calendar_permissions calendar_permissions_user_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY "public"."calendar_permissions"
    ADD CONSTRAINT "calendar_permissions_user_id_fkey" FOREIGN KEY ("user_id") REFERENCES "public"."users"("id") ON DELETE CASCADE;


--
-- Name: channel_members channel_members_channel_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY "public"."channel_members"
    ADD CONSTRAINT "channel_members_channel_id_fkey" FOREIGN KEY ("channel_id") REFERENCES "public"."channels"("id") ON DELETE CASCADE;


--
-- Name: channel_members channel_members_user_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY "public"."channel_members"
    ADD CONSTRAINT "channel_members_user_id_fkey" FOREIGN KEY ("user_id") REFERENCES "public"."users"("id") ON DELETE CASCADE;


--
-- Name: channel_read_state channel_read_state_channel_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY "public"."channel_read_state"
    ADD CONSTRAINT "channel_read_state_channel_id_fkey" FOREIGN KEY ("channel_id") REFERENCES "public"."channels"("id") ON DELETE CASCADE;


--
-- Name: channel_read_state channel_read_state_user_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY "public"."channel_read_state"
    ADD CONSTRAINT "channel_read_state_user_id_fkey" FOREIGN KEY ("user_id") REFERENCES "public"."users"("id") ON DELETE CASCADE;


--
-- Name: channels channels_created_by_fkey; Type: FK CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY "public"."channels"
    ADD CONSTRAINT "channels_created_by_fkey" FOREIGN KEY ("created_by") REFERENCES "public"."users"("id");


--
-- Name: channels channels_space_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY "public"."channels"
    ADD CONSTRAINT "channels_space_id_fkey" FOREIGN KEY ("space_id") REFERENCES "public"."spaces"("id") ON DELETE CASCADE;


--
-- Name: device_tokens device_tokens_user_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY "public"."device_tokens"
    ADD CONSTRAINT "device_tokens_user_id_fkey" FOREIGN KEY ("user_id") REFERENCES "public"."users"("id") ON DELETE CASCADE;


--
-- Name: invite_tokens invite_tokens_created_by_fkey; Type: FK CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY "public"."invite_tokens"
    ADD CONSTRAINT "invite_tokens_created_by_fkey" FOREIGN KEY ("created_by") REFERENCES "public"."users"("id");


--
-- Name: invite_tokens invite_tokens_used_by_fkey; Type: FK CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY "public"."invite_tokens"
    ADD CONSTRAINT "invite_tokens_used_by_fkey" FOREIGN KEY ("used_by") REFERENCES "public"."users"("id");


--
-- Name: invite_uses invite_uses_invite_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY "public"."invite_uses"
    ADD CONSTRAINT "invite_uses_invite_id_fkey" FOREIGN KEY ("invite_id") REFERENCES "public"."invite_tokens"("id") ON DELETE CASCADE;


--
-- Name: invite_uses invite_uses_used_by_fkey; Type: FK CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY "public"."invite_uses"
    ADD CONSTRAINT "invite_uses_used_by_fkey" FOREIGN KEY ("used_by") REFERENCES "public"."users"("id");


--
-- Name: join_requests join_requests_reviewed_by_fkey; Type: FK CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY "public"."join_requests"
    ADD CONSTRAINT "join_requests_reviewed_by_fkey" FOREIGN KEY ("reviewed_by") REFERENCES "public"."users"("id");


--
-- Name: mentions mentions_mentioned_user_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY "public"."mentions"
    ADD CONSTRAINT "mentions_mentioned_user_id_fkey" FOREIGN KEY ("mentioned_user_id") REFERENCES "public"."users"("id") ON DELETE CASCADE;


--
-- Name: mentions mentions_message_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY "public"."mentions"
    ADD CONSTRAINT "mentions_message_id_fkey" FOREIGN KEY ("message_id") REFERENCES "public"."messages"("id") ON DELETE CASCADE;


--
-- Name: messages messages_channel_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY "public"."messages"
    ADD CONSTRAINT "messages_channel_id_fkey" FOREIGN KEY ("channel_id") REFERENCES "public"."channels"("id") ON DELETE CASCADE;


--
-- Name: messages messages_reply_to_message_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY "public"."messages"
    ADD CONSTRAINT "messages_reply_to_message_id_fkey" FOREIGN KEY ("reply_to_message_id") REFERENCES "public"."messages"("id") ON DELETE SET NULL;


--
-- Name: messages messages_user_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY "public"."messages"
    ADD CONSTRAINT "messages_user_id_fkey" FOREIGN KEY ("user_id") REFERENCES "public"."users"("id");


--
-- Name: mfa_pending_tokens mfa_pending_tokens_user_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY "public"."mfa_pending_tokens"
    ADD CONSTRAINT "mfa_pending_tokens_user_id_fkey" FOREIGN KEY ("user_id") REFERENCES "public"."users"("id") ON DELETE CASCADE;


--
-- Name: notifications notifications_channel_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY "public"."notifications"
    ADD CONSTRAINT "notifications_channel_id_fkey" FOREIGN KEY ("channel_id") REFERENCES "public"."channels"("id") ON DELETE CASCADE;


--
-- Name: notifications notifications_source_user_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY "public"."notifications"
    ADD CONSTRAINT "notifications_source_user_id_fkey" FOREIGN KEY ("source_user_id") REFERENCES "public"."users"("id") ON DELETE SET NULL;


--
-- Name: notifications notifications_user_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY "public"."notifications"
    ADD CONSTRAINT "notifications_user_id_fkey" FOREIGN KEY ("user_id") REFERENCES "public"."users"("id") ON DELETE CASCADE;


--
-- Name: password_credentials password_credentials_user_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY "public"."password_credentials"
    ADD CONSTRAINT "password_credentials_user_id_fkey" FOREIGN KEY ("user_id") REFERENCES "public"."users"("id") ON DELETE CASCADE;


--
-- Name: password_history password_history_user_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY "public"."password_history"
    ADD CONSTRAINT "password_history_user_id_fkey" FOREIGN KEY ("user_id") REFERENCES "public"."users"("id") ON DELETE CASCADE;


--
-- Name: pki_credentials pki_credentials_user_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY "public"."pki_credentials"
    ADD CONSTRAINT "pki_credentials_user_id_fkey" FOREIGN KEY ("user_id") REFERENCES "public"."users"("id") ON DELETE CASCADE;


--
-- Name: reactions reactions_message_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY "public"."reactions"
    ADD CONSTRAINT "reactions_message_id_fkey" FOREIGN KEY ("message_id") REFERENCES "public"."messages"("id") ON DELETE CASCADE;


--
-- Name: reactions reactions_user_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY "public"."reactions"
    ADD CONSTRAINT "reactions_user_id_fkey" FOREIGN KEY ("user_id") REFERENCES "public"."users"("id") ON DELETE CASCADE;


--
-- Name: recovery_keys recovery_keys_user_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY "public"."recovery_keys"
    ADD CONSTRAINT "recovery_keys_user_id_fkey" FOREIGN KEY ("user_id") REFERENCES "public"."users"("id") ON DELETE CASCADE;


--
-- Name: recovery_tokens recovery_tokens_created_by_fkey; Type: FK CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY "public"."recovery_tokens"
    ADD CONSTRAINT "recovery_tokens_created_by_fkey" FOREIGN KEY ("created_by") REFERENCES "public"."users"("id");


--
-- Name: recovery_tokens recovery_tokens_for_user_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY "public"."recovery_tokens"
    ADD CONSTRAINT "recovery_tokens_for_user_id_fkey" FOREIGN KEY ("for_user_id") REFERENCES "public"."users"("id");


--
-- Name: sessions sessions_user_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY "public"."sessions"
    ADD CONSTRAINT "sessions_user_id_fkey" FOREIGN KEY ("user_id") REFERENCES "public"."users"("id") ON DELETE CASCADE;


--
-- Name: space_file_permissions space_file_permissions_file_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY "public"."space_file_permissions"
    ADD CONSTRAINT "space_file_permissions_file_id_fkey" FOREIGN KEY ("file_id") REFERENCES "public"."space_files"("id") ON DELETE CASCADE;


--
-- Name: space_file_permissions space_file_permissions_granted_by_fkey; Type: FK CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY "public"."space_file_permissions"
    ADD CONSTRAINT "space_file_permissions_granted_by_fkey" FOREIGN KEY ("granted_by") REFERENCES "public"."users"("id");


--
-- Name: space_file_permissions space_file_permissions_user_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY "public"."space_file_permissions"
    ADD CONSTRAINT "space_file_permissions_user_id_fkey" FOREIGN KEY ("user_id") REFERENCES "public"."users"("id") ON DELETE CASCADE;


--
-- Name: space_file_versions space_file_versions_file_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY "public"."space_file_versions"
    ADD CONSTRAINT "space_file_versions_file_id_fkey" FOREIGN KEY ("file_id") REFERENCES "public"."space_files"("id") ON DELETE CASCADE;


--
-- Name: space_file_versions space_file_versions_uploaded_by_fkey; Type: FK CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY "public"."space_file_versions"
    ADD CONSTRAINT "space_file_versions_uploaded_by_fkey" FOREIGN KEY ("uploaded_by") REFERENCES "public"."users"("id");


--
-- Name: space_files space_files_created_by_fkey; Type: FK CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY "public"."space_files"
    ADD CONSTRAINT "space_files_created_by_fkey" FOREIGN KEY ("created_by") REFERENCES "public"."users"("id");


--
-- Name: space_files space_files_parent_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY "public"."space_files"
    ADD CONSTRAINT "space_files_parent_id_fkey" FOREIGN KEY ("parent_id") REFERENCES "public"."space_files"("id") ON DELETE CASCADE;


--
-- Name: space_files space_files_space_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY "public"."space_files"
    ADD CONSTRAINT "space_files_space_id_fkey" FOREIGN KEY ("space_id") REFERENCES "public"."spaces"("id") ON DELETE CASCADE;


--
-- Name: space_invites space_invites_invited_by_fkey; Type: FK CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY "public"."space_invites"
    ADD CONSTRAINT "space_invites_invited_by_fkey" FOREIGN KEY ("invited_by") REFERENCES "public"."users"("id") ON DELETE CASCADE;


--
-- Name: space_invites space_invites_invited_user_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY "public"."space_invites"
    ADD CONSTRAINT "space_invites_invited_user_id_fkey" FOREIGN KEY ("invited_user_id") REFERENCES "public"."users"("id") ON DELETE CASCADE;


--
-- Name: space_invites space_invites_space_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY "public"."space_invites"
    ADD CONSTRAINT "space_invites_space_id_fkey" FOREIGN KEY ("space_id") REFERENCES "public"."spaces"("id") ON DELETE CASCADE;


--
-- Name: space_members space_members_space_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY "public"."space_members"
    ADD CONSTRAINT "space_members_space_id_fkey" FOREIGN KEY ("space_id") REFERENCES "public"."spaces"("id") ON DELETE CASCADE;


--
-- Name: space_members space_members_user_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY "public"."space_members"
    ADD CONSTRAINT "space_members_user_id_fkey" FOREIGN KEY ("user_id") REFERENCES "public"."users"("id") ON DELETE CASCADE;


--
-- Name: space_tools space_tools_enabled_by_fkey; Type: FK CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY "public"."space_tools"
    ADD CONSTRAINT "space_tools_enabled_by_fkey" FOREIGN KEY ("enabled_by") REFERENCES "public"."users"("id");


--
-- Name: space_tools space_tools_space_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY "public"."space_tools"
    ADD CONSTRAINT "space_tools_space_id_fkey" FOREIGN KEY ("space_id") REFERENCES "public"."spaces"("id") ON DELETE CASCADE;


--
-- Name: spaces spaces_created_by_fkey; Type: FK CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY "public"."spaces"
    ADD CONSTRAINT "spaces_created_by_fkey" FOREIGN KEY ("created_by") REFERENCES "public"."users"("id");


--
-- Name: spaces spaces_personal_owner_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY "public"."spaces"
    ADD CONSTRAINT "spaces_personal_owner_id_fkey" FOREIGN KEY ("personal_owner_id") REFERENCES "public"."users"("id") ON DELETE CASCADE;


--
-- Name: task_activity task_activity_task_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY "public"."task_activity"
    ADD CONSTRAINT "task_activity_task_id_fkey" FOREIGN KEY ("task_id") REFERENCES "public"."tasks"("id") ON DELETE CASCADE;


--
-- Name: task_activity task_activity_user_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY "public"."task_activity"
    ADD CONSTRAINT "task_activity_user_id_fkey" FOREIGN KEY ("user_id") REFERENCES "public"."users"("id");


--
-- Name: task_assignees task_assignees_task_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY "public"."task_assignees"
    ADD CONSTRAINT "task_assignees_task_id_fkey" FOREIGN KEY ("task_id") REFERENCES "public"."tasks"("id") ON DELETE CASCADE;


--
-- Name: task_assignees task_assignees_user_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY "public"."task_assignees"
    ADD CONSTRAINT "task_assignees_user_id_fkey" FOREIGN KEY ("user_id") REFERENCES "public"."users"("id") ON DELETE CASCADE;


--
-- Name: task_board_permissions task_board_permissions_granted_by_fkey; Type: FK CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY "public"."task_board_permissions"
    ADD CONSTRAINT "task_board_permissions_granted_by_fkey" FOREIGN KEY ("granted_by") REFERENCES "public"."users"("id");


--
-- Name: task_board_permissions task_board_permissions_space_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY "public"."task_board_permissions"
    ADD CONSTRAINT "task_board_permissions_space_id_fkey" FOREIGN KEY ("space_id") REFERENCES "public"."spaces"("id") ON DELETE CASCADE;


--
-- Name: task_board_permissions task_board_permissions_user_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY "public"."task_board_permissions"
    ADD CONSTRAINT "task_board_permissions_user_id_fkey" FOREIGN KEY ("user_id") REFERENCES "public"."users"("id") ON DELETE CASCADE;


--
-- Name: task_boards task_boards_created_by_fkey; Type: FK CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY "public"."task_boards"
    ADD CONSTRAINT "task_boards_created_by_fkey" FOREIGN KEY ("created_by") REFERENCES "public"."users"("id");


--
-- Name: task_boards task_boards_space_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY "public"."task_boards"
    ADD CONSTRAINT "task_boards_space_id_fkey" FOREIGN KEY ("space_id") REFERENCES "public"."spaces"("id") ON DELETE CASCADE;


--
-- Name: task_checklist_items task_checklist_items_checklist_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY "public"."task_checklist_items"
    ADD CONSTRAINT "task_checklist_items_checklist_id_fkey" FOREIGN KEY ("checklist_id") REFERENCES "public"."task_checklists"("id") ON DELETE CASCADE;


--
-- Name: task_checklists task_checklists_task_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY "public"."task_checklists"
    ADD CONSTRAINT "task_checklists_task_id_fkey" FOREIGN KEY ("task_id") REFERENCES "public"."tasks"("id") ON DELETE CASCADE;


--
-- Name: task_columns task_columns_board_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY "public"."task_columns"
    ADD CONSTRAINT "task_columns_board_id_fkey" FOREIGN KEY ("board_id") REFERENCES "public"."task_boards"("id") ON DELETE CASCADE;


--
-- Name: task_dependencies task_dependencies_depends_on_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY "public"."task_dependencies"
    ADD CONSTRAINT "task_dependencies_depends_on_id_fkey" FOREIGN KEY ("depends_on_id") REFERENCES "public"."tasks"("id") ON DELETE CASCADE;


--
-- Name: task_dependencies task_dependencies_task_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY "public"."task_dependencies"
    ADD CONSTRAINT "task_dependencies_task_id_fkey" FOREIGN KEY ("task_id") REFERENCES "public"."tasks"("id") ON DELETE CASCADE;


--
-- Name: task_label_assignments task_label_assignments_label_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY "public"."task_label_assignments"
    ADD CONSTRAINT "task_label_assignments_label_id_fkey" FOREIGN KEY ("label_id") REFERENCES "public"."task_labels"("id") ON DELETE CASCADE;


--
-- Name: task_label_assignments task_label_assignments_task_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY "public"."task_label_assignments"
    ADD CONSTRAINT "task_label_assignments_task_id_fkey" FOREIGN KEY ("task_id") REFERENCES "public"."tasks"("id") ON DELETE CASCADE;


--
-- Name: task_labels task_labels_board_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY "public"."task_labels"
    ADD CONSTRAINT "task_labels_board_id_fkey" FOREIGN KEY ("board_id") REFERENCES "public"."task_boards"("id") ON DELETE CASCADE;


--
-- Name: tasks tasks_board_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY "public"."tasks"
    ADD CONSTRAINT "tasks_board_id_fkey" FOREIGN KEY ("board_id") REFERENCES "public"."task_boards"("id") ON DELETE CASCADE;


--
-- Name: tasks tasks_column_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY "public"."tasks"
    ADD CONSTRAINT "tasks_column_id_fkey" FOREIGN KEY ("column_id") REFERENCES "public"."task_columns"("id") ON DELETE CASCADE;


--
-- Name: tasks tasks_created_by_fkey; Type: FK CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY "public"."tasks"
    ADD CONSTRAINT "tasks_created_by_fkey" FOREIGN KEY ("created_by") REFERENCES "public"."users"("id");


--
-- Name: totp_credentials totp_credentials_user_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY "public"."totp_credentials"
    ADD CONSTRAINT "totp_credentials_user_id_fkey" FOREIGN KEY ("user_id") REFERENCES "public"."users"("id") ON DELETE CASCADE;


--
-- Name: user_keys user_keys_user_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY "public"."user_keys"
    ADD CONSTRAINT "user_keys_user_id_fkey" FOREIGN KEY ("user_id") REFERENCES "public"."users"("id") ON DELETE CASCADE;


--
-- Name: user_settings user_settings_user_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY "public"."user_settings"
    ADD CONSTRAINT "user_settings_user_id_fkey" FOREIGN KEY ("user_id") REFERENCES "public"."users"("id") ON DELETE CASCADE;


--
-- Name: users users_banned_by_fkey; Type: FK CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY "public"."users"
    ADD CONSTRAINT "users_banned_by_fkey" FOREIGN KEY ("banned_by") REFERENCES "public"."users"("id");


--
-- Name: webauthn_credentials webauthn_credentials_user_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY "public"."webauthn_credentials"
    ADD CONSTRAINT "webauthn_credentials_user_id_fkey" FOREIGN KEY ("user_id") REFERENCES "public"."users"("id") ON DELETE CASCADE;


--
-- Name: wiki_page_permissions wiki_page_permissions_granted_by_fkey; Type: FK CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY "public"."wiki_page_permissions"
    ADD CONSTRAINT "wiki_page_permissions_granted_by_fkey" FOREIGN KEY ("granted_by") REFERENCES "public"."users"("id");


--
-- Name: wiki_page_permissions wiki_page_permissions_page_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY "public"."wiki_page_permissions"
    ADD CONSTRAINT "wiki_page_permissions_page_id_fkey" FOREIGN KEY ("page_id") REFERENCES "public"."wiki_pages"("id") ON DELETE CASCADE;


--
-- Name: wiki_page_permissions wiki_page_permissions_user_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY "public"."wiki_page_permissions"
    ADD CONSTRAINT "wiki_page_permissions_user_id_fkey" FOREIGN KEY ("user_id") REFERENCES "public"."users"("id") ON DELETE CASCADE;


--
-- Name: wiki_page_versions wiki_page_versions_edited_by_fkey; Type: FK CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY "public"."wiki_page_versions"
    ADD CONSTRAINT "wiki_page_versions_edited_by_fkey" FOREIGN KEY ("edited_by") REFERENCES "public"."users"("id");


--
-- Name: wiki_page_versions wiki_page_versions_page_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY "public"."wiki_page_versions"
    ADD CONSTRAINT "wiki_page_versions_page_id_fkey" FOREIGN KEY ("page_id") REFERENCES "public"."wiki_pages"("id") ON DELETE CASCADE;


--
-- Name: wiki_pages wiki_pages_created_by_fkey; Type: FK CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY "public"."wiki_pages"
    ADD CONSTRAINT "wiki_pages_created_by_fkey" FOREIGN KEY ("created_by") REFERENCES "public"."users"("id");


--
-- Name: wiki_pages wiki_pages_last_edited_by_fkey; Type: FK CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY "public"."wiki_pages"
    ADD CONSTRAINT "wiki_pages_last_edited_by_fkey" FOREIGN KEY ("last_edited_by") REFERENCES "public"."users"("id");


--
-- Name: wiki_pages wiki_pages_parent_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY "public"."wiki_pages"
    ADD CONSTRAINT "wiki_pages_parent_id_fkey" FOREIGN KEY ("parent_id") REFERENCES "public"."wiki_pages"("id") ON DELETE CASCADE;


--
-- Name: wiki_pages wiki_pages_space_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY "public"."wiki_pages"
    ADD CONSTRAINT "wiki_pages_space_id_fkey" FOREIGN KEY ("space_id") REFERENCES "public"."spaces"("id") ON DELETE CASCADE;


--
-- Name: wiki_permissions wiki_permissions_granted_by_fkey; Type: FK CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY "public"."wiki_permissions"
    ADD CONSTRAINT "wiki_permissions_granted_by_fkey" FOREIGN KEY ("granted_by") REFERENCES "public"."users"("id");


--
-- Name: wiki_permissions wiki_permissions_space_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY "public"."wiki_permissions"
    ADD CONSTRAINT "wiki_permissions_space_id_fkey" FOREIGN KEY ("space_id") REFERENCES "public"."spaces"("id") ON DELETE CASCADE;


--
-- Name: wiki_permissions wiki_permissions_user_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY "public"."wiki_permissions"
    ADD CONSTRAINT "wiki_permissions_user_id_fkey" FOREIGN KEY ("user_id") REFERENCES "public"."users"("id") ON DELETE CASCADE;


--
-- PostgreSQL database dump complete
--



COMMIT;
