create extension if not exists pgcrypto;

create table auth_users (
                            id uuid primary key default gen_random_uuid(),
                            google_sub text unique not null,
                            email text not null,
                            name text,
                            picture_url text,
                            created_at timestamptz not null default now(),
                            updated_at timestamptz not null default now(),
                            last_login_at timestamptz
);

create table google_oauth_tokens (
                                     user_id uuid primary key references auth_users(id) on delete cascade,
                                     access_token_enc text not null,
                                     refresh_token_enc text,
                                     expires_at timestamptz not null,
                                     scope text,
                                     token_type text,
                                     created_at timestamptz not null default now(),
                                     updated_at timestamptz not null default now()
);

create table app_sessions (
                              id uuid primary key default gen_random_uuid(),
                              user_id uuid not null references auth_users(id) on delete cascade,
                              session_hash text unique not null,
                              expires_at timestamptz not null,
                              revoked_at timestamptz,
                              created_at timestamptz not null default now(),
                              last_seen_at timestamptz,
                              user_agent text
);

create table oauth_states (
                              state_hash text primary key,
                              expires_at timestamptz not null,
                              consumed_at timestamptz,
                              created_at timestamptz not null default now()
);

create table documents (
                           id uuid primary key default gen_random_uuid(),
                           external_id text unique not null,
                           title text
);

create table document_sections (
                                   id uuid primary key default gen_random_uuid(),
                                   document_id uuid not null references documents(id) on delete cascade,
                                   title text not null,
                                   content text not null
);

create index idx_document_sections_document_id
    on document_sections(document_id);