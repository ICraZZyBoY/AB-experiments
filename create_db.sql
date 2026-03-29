CREATE SCHEMA IF NOT EXISTS abtest;
SET search_path TO abtest;

------------------------------------------------------------
-- 1. Клиентские сервисы и пользователи платформы
------------------------------------------------------------

CREATE TABLE ClientService (
    id           BIGSERIAL PRIMARY KEY,
    name         TEXT        NOT NULL,
    description  TEXT,
    status       TEXT        NOT NULL DEFAULT 'ACTIVE',
    created_at   TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

CREATE TABLE PlatformUser (
    id                BIGSERIAL PRIMARY KEY,
    client_service_id BIGINT     NOT NULL REFERENCES ClientService(id),
    email             TEXT       NOT NULL,
    full_name         TEXT,
    password_hash     TEXT       NOT NULL,
    status            TEXT       NOT NULL DEFAULT 'ACTIVE',
    created_at        TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    CONSTRAINT uq_platform_user_email_per_service
        UNIQUE (client_service_id, email)
);

CREATE TABLE PlatformRole (
    id          BIGSERIAL PRIMARY KEY,
    code        TEXT NOT NULL,  -- ADMIN, ANALYST, DEVELOPER ...
    name        TEXT NOT NULL,
    description TEXT,
    CONSTRAINT uq_platform_role_code UNIQUE (code)
);

CREATE TABLE PlatformUserRole (
    platform_user_id BIGINT NOT NULL REFERENCES PlatformUser(id),
    platform_role_id BIGINT NOT NULL REFERENCES PlatformRole(id),
    PRIMARY KEY (platform_user_id, platform_role_id)
);

CREATE TABLE EndUser (
    id                BIGSERIAL PRIMARY KEY,
    client_service_id BIGINT     NOT NULL REFERENCES ClientService(id),
    external_user_id  TEXT,      -- ID в системе клиента
    device_id         TEXT,
    ip_address        INET,
    created_at        TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    -- по желанию можно запретить дубли external_user_id внутри сервиса:
    -- CONSTRAINT uq_enduser_external_per_service
    --     UNIQUE (client_service_id, external_user_id)
    CONSTRAINT uq_enduser_device_per_service
        UNIQUE (client_service_id, device_id)
);

CREATE TABLE ApiKey (
    id                BIGSERIAL PRIMARY KEY,
    client_service_id BIGINT     NOT NULL REFERENCES ClientService(id),
    name              TEXT       NOT NULL,
    key_hash          TEXT       NOT NULL,
    status            TEXT       NOT NULL DEFAULT 'ACTIVE',
    created_at        TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    revoked_at        TIMESTAMPTZ,
    CONSTRAINT uq_apikey_hash UNIQUE (key_hash)
);

------------------------------------------------------------
-- 2. Слои и эксперименты
------------------------------------------------------------

CREATE TABLE ExperimentLayer (
    id                BIGSERIAL PRIMARY KEY,
    client_service_id BIGINT     NOT NULL REFERENCES ClientService(id),
    name              TEXT       NOT NULL,
    description       TEXT,
    created_at        TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    CONSTRAINT uq_layer_name_per_service
        UNIQUE (client_service_id, name)
);

CREATE TABLE Experiment (
    id                  BIGSERIAL PRIMARY KEY,
    client_service_id   BIGINT     NOT NULL REFERENCES ClientService(id),
    experiment_layer_id BIGINT     NOT NULL REFERENCES ExperimentLayer(id),
    created_by_user_id  BIGINT     NOT NULL REFERENCES PlatformUser(id),
    name                TEXT       NOT NULL,
    description         TEXT,
    status              TEXT       NOT NULL DEFAULT 'DRAFT', -- DRAFT/RUNNING/...
    salt                TEXT       NOT NULL,                 -- для рандомизации
    start_at            TIMESTAMPTZ,
    end_at              TIMESTAMPTZ,
    created_at          TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    CONSTRAINT uq_experiment_name_per_service
        UNIQUE (client_service_id, name)
);

CREATE TABLE ExperimentVariant (
    id             BIGSERIAL PRIMARY KEY,
    experiment_id  BIGINT NOT NULL REFERENCES Experiment(id),
    key            TEXT   NOT NULL,      -- "A", "B", "C" ...
    name           TEXT   NOT NULL,
    description    TEXT,
    traffic_weight DOUBLE PRECISION NOT NULL,  -- доля трафика 0..1
    CONSTRAINT uq_variant_key_per_experiment
        UNIQUE (experiment_id, key)
);

------------------------------------------------------------
-- 3. Измеряемые сущности: фичи, метрики, срезы, флаги
------------------------------------------------------------

CREATE TABLE Feature (
    id                BIGSERIAL PRIMARY KEY,
    client_service_id BIGINT NOT NULL REFERENCES ClientService(id),
    key               TEXT   NOT NULL,  -- уникальный ключ типа события
    name              TEXT   NOT NULL,
    description       TEXT,
    CONSTRAINT uq_feature_key_per_service
        UNIQUE (client_service_id, key)
);

CREATE TABLE Metric (
    id                BIGSERIAL PRIMARY KEY,
    client_service_id BIGINT NOT NULL REFERENCES ClientService(id),
    code              TEXT   NOT NULL,  -- уникальный код метрики
    name              TEXT   NOT NULL,
    description       TEXT,
    metric_type       TEXT   NOT NULL,  -- CONVERSION, MEAN, SUM ...
    aggregation_unit  TEXT   NOT NULL,  -- PER_USER, PER_SESSION, ...
    definition        TEXT   NOT NULL,  -- JSON/DSL
    CONSTRAINT uq_metric_code_per_service
        UNIQUE (client_service_id, code)
);

CREATE TABLE Slice (
    id                BIGSERIAL PRIMARY KEY,
    client_service_id BIGINT NOT NULL REFERENCES ClientService(id),
    name              TEXT   NOT NULL,
    description       TEXT,
    definition        TEXT   NOT NULL,  -- JSON/DSL с фильтрами/группировкой
    CONSTRAINT uq_slice_name_per_service
        UNIQUE (client_service_id, name)
);

CREATE TABLE Flag (
    id                BIGSERIAL PRIMARY KEY,
    client_service_id BIGINT NOT NULL REFERENCES ClientService(id),
    key               TEXT   NOT NULL,  -- ключ флага
    name              TEXT   NOT NULL,
    description       TEXT,
    value_type        TEXT   NOT NULL,  -- BOOL/INT/STRING/JSON
    default_value     TEXT,
    CONSTRAINT uq_flag_key_per_service
        UNIQUE (client_service_id, key)
);

CREATE TABLE VariantFlag (
    experiment_variant_id BIGINT NOT NULL REFERENCES ExperimentVariant(id),
    flag_id               BIGINT NOT NULL REFERENCES Flag(id),
    value                 TEXT   NOT NULL, -- значение флага в данном варианте
    PRIMARY KEY (experiment_variant_id, flag_id)
);

------------------------------------------------------------
-- 4. Логи назначений и событий
------------------------------------------------------------

CREATE TABLE AssignmentLog (
    id                    BIGSERIAL PRIMARY KEY,
    client_service_id     BIGINT     NOT NULL REFERENCES ClientService(id),
    end_user_id           BIGINT     NOT NULL REFERENCES EndUser(id),
    experiment_id         BIGINT     NOT NULL REFERENCES Experiment(id),
    experiment_variant_id BIGINT     NOT NULL REFERENCES ExperimentVariant(id),
    req_id                TEXT       NOT NULL,
    assigned_at           TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

CREATE INDEX idx_assignmentlog_req
    ON AssignmentLog (client_service_id, req_id);

CREATE INDEX idx_assignmentlog_experiment
    ON AssignmentLog (experiment_id, experiment_variant_id, assigned_at);

CREATE TABLE EventsLog (
    id                BIGSERIAL PRIMARY KEY,
    client_service_id BIGINT     NOT NULL REFERENCES ClientService(id),
    end_user_id       BIGINT     NOT NULL REFERENCES EndUser(id),
    feature_id        BIGINT     NOT NULL REFERENCES Feature(id),
    req_id            TEXT,
    occurred_at       TIMESTAMPTZ NOT NULL,
    value             DOUBLE PRECISION,
    properties        JSONB
);

CREATE INDEX idx_eventslog_req
    ON EventsLog (client_service_id, req_id);

CREATE INDEX idx_eventslog_feature_time
    ON EventsLog (feature_id, occurred_at);

CREATE INDEX idx_eventslog_enduser_time
    ON EventsLog (end_user_id, occurred_at);

------------------------------------------------------------
-- 5. Связки «эксперимент–метрика», «эксперимент–срез»
------------------------------------------------------------

CREATE TABLE ExperimentMetric (
    experiment_id BIGINT   NOT NULL REFERENCES Experiment(id),
    metric_id     BIGINT   NOT NULL REFERENCES Metric(id),
    is_primary    BOOLEAN  NOT NULL DEFAULT FALSE,
    is_guardrail  BOOLEAN  NOT NULL DEFAULT FALSE,
    PRIMARY KEY (experiment_id, metric_id)
);

CREATE TABLE ExperimentSlice (
    experiment_id BIGINT NOT NULL REFERENCES Experiment(id),
    slice_id      BIGINT NOT NULL REFERENCES Slice(id),
    PRIMARY KEY (experiment_id, slice_id)
);

------------------------------------------------------------
-- 6. Результаты метрик
------------------------------------------------------------

CREATE TABLE MetricResult (
    id                    BIGSERIAL PRIMARY KEY,
    experiment_id         BIGINT NOT NULL REFERENCES Experiment(id),
    experiment_variant_id BIGINT NOT NULL REFERENCES ExperimentVariant(id),
    metric_id             BIGINT NOT NULL REFERENCES Metric(id),
    slice_id              BIGINT REFERENCES Slice(id),
    period_start          TIMESTAMPTZ NOT NULL,
    period_end            TIMESTAMPTZ NOT NULL,
    value                 DOUBLE PRECISION NOT NULL,
    std_error             DOUBLE PRECISION,
    ci_low                DOUBLE PRECISION,
    ci_high               DOUBLE PRECISION,
    p_value               DOUBLE PRECISION,
    lift                  DOUBLE PRECISION,
    CONSTRAINT uq_metricresult_unique_bucket
        UNIQUE (experiment_id, experiment_variant_id, metric_id, slice_id, period_start, period_end)
);

CREATE INDEX idx_metricresult_experiment
    ON MetricResult (experiment_id, experiment_variant_id);

CREATE INDEX idx_metricresult_period
    ON MetricResult (period_start, period_end);

------------------------------------------------------------
-- 7. Конфиги и аудит
------------------------------------------------------------

CREATE TABLE ConfigVersion (
    id                BIGSERIAL PRIMARY KEY,
    client_service_id BIGINT     NOT NULL REFERENCES ClientService(id),
    version           INT        NOT NULL,
    created_at        TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    config_json       JSONB      NOT NULL,
    CONSTRAINT uq_configversion_per_service
        UNIQUE (client_service_id, version)
);

CREATE TABLE AuditLog (
    id                BIGSERIAL PRIMARY KEY,
    client_service_id BIGINT     NOT NULL REFERENCES ClientService(id),
    platform_user_id  BIGINT     NOT NULL REFERENCES PlatformUser(id),
    entity_type       TEXT       NOT NULL,  -- 'Experiment', 'Metric', и т.п.
    entity_id         BIGINT     NOT NULL,
    action            TEXT       NOT NULL,  -- 'CREATE', 'UPDATE', 'DELETE', 'START', 'STOP'
    created_at        TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    details           JSONB
);

CREATE INDEX idx_auditlog_entity
    ON AuditLog (entity_type, entity_id, created_at);
