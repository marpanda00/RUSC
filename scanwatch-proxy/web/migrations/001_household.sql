create table if not exists household_state (
  id integer primary key,
  floor integer,
  watch_connected boolean not null default false,
  pending_vibrations integer,
  last_message text,
  last_error text,
  updated_at timestamptz not null default now(),
  constraint household_single_row check (id = 1)
);

insert into household_state (id) values (1)
  on conflict (id) do nothing;
