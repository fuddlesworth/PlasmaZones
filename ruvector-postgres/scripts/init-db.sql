-- ============================================
-- RUVECTOR POSTGRESQL INITIALIZATION SCRIPT
-- ============================================
--
-- This script initializes RuVector PostgreSQL extension
-- from ruvnet/ruvector-postgres with Claude-Flow V3 integration.
--
-- RuVector provides 77+ SQL functions including:
-- - Vector similarity search (HNSW with SIMD)
-- - Hyperbolic embeddings (Poincaré/Lorentz)
-- - Graph operations (Cypher queries)
-- - Agent routing and learning
--
-- Performance: ~61µs latency, 16,400 QPS

-- ============================================
-- PART 1: EXTENSION AND SCHEMA SETUP
-- ============================================

-- IMPORTANT: RuVector requires explicit VERSION
-- The control file says 2.0.0 but only 0.1.0 SQL exists
CREATE EXTENSION IF NOT EXISTS ruvector VERSION '0.1.0';

-- Enable additional required extensions
CREATE EXTENSION IF NOT EXISTS pgcrypto;

-- Create the claude_flow schema
CREATE SCHEMA IF NOT EXISTS claude_flow;

-- Grant permissions
GRANT ALL ON SCHEMA claude_flow TO claude;

-- Set search path
SET search_path TO claude_flow, public;

-- ============================================
-- PART 2: CORE TABLES
-- ============================================

-- Embeddings table with RuVector vector type (384-dim for all-MiniLM-L6-v2)
CREATE TABLE IF NOT EXISTS claude_flow.embeddings (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    content TEXT NOT NULL,
    embedding ruvector(384),
    metadata JSONB DEFAULT '{}',
    namespace VARCHAR(100) DEFAULT 'default',
    created_at TIMESTAMPTZ DEFAULT NOW(),
    updated_at TIMESTAMPTZ DEFAULT NOW()
);

-- Patterns table for learned patterns (ReasoningBank)
CREATE TABLE IF NOT EXISTS claude_flow.patterns (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    name VARCHAR(255) NOT NULL,
    description TEXT,
    embedding ruvector(384),
    pattern_type VARCHAR(50),
    confidence FLOAT DEFAULT 0.5,
    success_count INT DEFAULT 0,
    failure_count INT DEFAULT 0,
    ewc_importance FLOAT DEFAULT 1.0,
    metadata JSONB DEFAULT '{}',
    created_at TIMESTAMPTZ DEFAULT NOW()
);

-- Agents table for multi-agent memory coordination
CREATE TABLE IF NOT EXISTS claude_flow.agents (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    agent_id VARCHAR(255) NOT NULL UNIQUE,
    agent_type VARCHAR(50),
    state JSONB DEFAULT '{}',
    memory_embedding ruvector(384),
    last_active TIMESTAMPTZ DEFAULT NOW(),
    created_at TIMESTAMPTZ DEFAULT NOW()
);

-- Trajectories table for SONA reinforcement learning
CREATE TABLE IF NOT EXISTS claude_flow.trajectories (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    trajectory_id VARCHAR(255) NOT NULL UNIQUE,
    agent_type VARCHAR(50),
    task_description TEXT,
    status VARCHAR(20) DEFAULT 'in_progress',
    steps JSONB DEFAULT '[]',
    outcome VARCHAR(20),
    quality_score FLOAT,
    started_at TIMESTAMPTZ DEFAULT NOW(),
    ended_at TIMESTAMPTZ
);

-- Memory entries table (main storage for Claude-Flow memory)
CREATE TABLE IF NOT EXISTS claude_flow.memory_entries (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    key VARCHAR(255) NOT NULL,
    value TEXT NOT NULL,
    embedding ruvector(384),
    namespace VARCHAR(100) DEFAULT 'default',
    metadata JSONB DEFAULT '{}',
    ttl TIMESTAMPTZ,
    created_at TIMESTAMPTZ DEFAULT NOW(),
    updated_at TIMESTAMPTZ DEFAULT NOW(),
    UNIQUE(key, namespace)
);

-- Hyperbolic embeddings for hierarchical data
CREATE TABLE IF NOT EXISTS claude_flow.hyperbolic_embeddings (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    content TEXT NOT NULL,
    euclidean_embedding ruvector(384),
    poincare_embedding real[],  -- Array for hyperbolic operations
    curvature FLOAT DEFAULT -1.0,
    hierarchy_level INT DEFAULT 0,
    parent_id UUID REFERENCES claude_flow.hyperbolic_embeddings(id),
    metadata JSONB DEFAULT '{}',
    created_at TIMESTAMPTZ DEFAULT NOW()
);

-- Graph nodes for GNN operations
CREATE TABLE IF NOT EXISTS claude_flow.graph_nodes (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    node_id VARCHAR(255) NOT NULL UNIQUE,
    node_type VARCHAR(50),
    embedding ruvector(384),
    features JSONB DEFAULT '{}',
    created_at TIMESTAMPTZ DEFAULT NOW()
);

-- Graph edges for message passing
CREATE TABLE IF NOT EXISTS claude_flow.graph_edges (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    source_id UUID REFERENCES claude_flow.graph_nodes(id),
    target_id UUID REFERENCES claude_flow.graph_nodes(id),
    edge_type VARCHAR(50),
    weight FLOAT DEFAULT 1.0,
    metadata JSONB DEFAULT '{}',
    created_at TIMESTAMPTZ DEFAULT NOW()
);

-- ============================================
-- PART 3: HNSW INDICES (150x-12,500x faster)
-- ============================================

-- HNSW index for embeddings (cosine distance)
CREATE INDEX IF NOT EXISTS idx_embeddings_hnsw
ON claude_flow.embeddings
USING hnsw (embedding ruvector_cosine_ops)
WITH (m = 16, ef_construction = 100);

-- HNSW index for patterns
CREATE INDEX IF NOT EXISTS idx_patterns_hnsw
ON claude_flow.patterns
USING hnsw (embedding ruvector_cosine_ops)
WITH (m = 16, ef_construction = 100);

-- HNSW index for agent memory
CREATE INDEX IF NOT EXISTS idx_agents_hnsw
ON claude_flow.agents
USING hnsw (memory_embedding ruvector_cosine_ops)
WITH (m = 16, ef_construction = 64);

-- HNSW index for memory entries
CREATE INDEX IF NOT EXISTS idx_memory_entries_hnsw
ON claude_flow.memory_entries
USING hnsw (embedding ruvector_cosine_ops)
WITH (m = 16, ef_construction = 100);

-- HNSW index for hyperbolic embeddings
CREATE INDEX IF NOT EXISTS idx_hyperbolic_hnsw
ON claude_flow.hyperbolic_embeddings
USING hnsw (euclidean_embedding ruvector_cosine_ops)
WITH (m = 16, ef_construction = 100);

-- HNSW index for graph nodes
CREATE INDEX IF NOT EXISTS idx_graph_nodes_hnsw
ON claude_flow.graph_nodes
USING hnsw (embedding ruvector_cosine_ops)
WITH (m = 16, ef_construction = 64);

-- Additional indices for common queries
CREATE INDEX IF NOT EXISTS idx_embeddings_namespace ON claude_flow.embeddings(namespace);
CREATE INDEX IF NOT EXISTS idx_memory_entries_namespace ON claude_flow.memory_entries(namespace);
CREATE INDEX IF NOT EXISTS idx_memory_entries_key ON claude_flow.memory_entries(key);

-- ============================================
-- PART 4: CORE SEARCH FUNCTIONS
-- ============================================

-- Semantic similarity search using RuVector HNSW
CREATE OR REPLACE FUNCTION claude_flow.search_similar(
    query_embedding ruvector(384),
    limit_count INT DEFAULT 10,
    min_similarity FLOAT DEFAULT 0.5
)
RETURNS TABLE (
    id UUID,
    content TEXT,
    similarity FLOAT,
    metadata JSONB
) AS $$
BEGIN
    RETURN QUERY
    SELECT
        e.id,
        e.content,
        (1 - (e.embedding <=> query_embedding))::FLOAT AS similarity,
        e.metadata
    FROM claude_flow.embeddings e
    WHERE e.embedding IS NOT NULL
      AND (1 - (e.embedding <=> query_embedding)) >= min_similarity
    ORDER BY e.embedding <=> query_embedding
    LIMIT limit_count;
END;
$$ LANGUAGE plpgsql STABLE;

-- Memory search with namespace filtering
CREATE OR REPLACE FUNCTION claude_flow.search_memory(
    query_embedding ruvector(384),
    namespace_filter VARCHAR(100) DEFAULT NULL,
    limit_count INT DEFAULT 10,
    min_similarity FLOAT DEFAULT 0.5
)
RETURNS TABLE (
    id UUID,
    key VARCHAR(255),
    value TEXT,
    namespace VARCHAR(100),
    similarity FLOAT,
    metadata JSONB
) AS $$
BEGIN
    RETURN QUERY
    SELECT
        m.id,
        m.key,
        m.value,
        m.namespace,
        (1 - (m.embedding <=> query_embedding))::FLOAT AS similarity,
        m.metadata
    FROM claude_flow.memory_entries m
    WHERE m.embedding IS NOT NULL
      AND (1 - (m.embedding <=> query_embedding)) >= min_similarity
      AND (namespace_filter IS NULL OR m.namespace = namespace_filter)
      AND (m.ttl IS NULL OR m.ttl > NOW())
    ORDER BY m.embedding <=> query_embedding
    LIMIT limit_count;
END;
$$ LANGUAGE plpgsql STABLE;

-- Pattern search with type filtering
CREATE OR REPLACE FUNCTION claude_flow.search_patterns(
    query_embedding ruvector(384),
    pattern_type_filter VARCHAR(50) DEFAULT NULL,
    limit_count INT DEFAULT 10,
    min_confidence FLOAT DEFAULT 0.5
)
RETURNS TABLE (
    id UUID,
    name VARCHAR(255),
    description TEXT,
    similarity FLOAT,
    confidence FLOAT,
    metadata JSONB
) AS $$
BEGIN
    RETURN QUERY
    SELECT
        p.id,
        p.name,
        p.description,
        (1 - (p.embedding <=> query_embedding))::FLOAT AS similarity,
        p.confidence,
        p.metadata
    FROM claude_flow.patterns p
    WHERE p.embedding IS NOT NULL
      AND p.confidence >= min_confidence
      AND (pattern_type_filter IS NULL OR p.pattern_type = pattern_type_filter)
    ORDER BY p.embedding <=> query_embedding
    LIMIT limit_count;
END;
$$ LANGUAGE plpgsql STABLE;

-- Agent routing by expertise similarity
CREATE OR REPLACE FUNCTION claude_flow.find_agents(
    query_embedding ruvector(384),
    agent_type_filter VARCHAR(50) DEFAULT NULL,
    limit_count INT DEFAULT 5
)
RETURNS TABLE (
    agent_id VARCHAR(255),
    agent_type VARCHAR(50),
    similarity FLOAT,
    state JSONB
) AS $$
BEGIN
    RETURN QUERY
    SELECT
        a.agent_id,
        a.agent_type,
        (1 - (a.memory_embedding <=> query_embedding))::FLOAT AS similarity,
        a.state
    FROM claude_flow.agents a
    WHERE a.memory_embedding IS NOT NULL
      AND (agent_type_filter IS NULL OR a.agent_type = agent_type_filter)
    ORDER BY a.memory_embedding <=> query_embedding
    LIMIT limit_count;
END;
$$ LANGUAGE plpgsql STABLE;

-- ============================================
-- PART 5: HYPERBOLIC OPERATIONS
-- ============================================

-- Convert Euclidean to Poincaré embedding
CREATE OR REPLACE FUNCTION claude_flow.to_poincare(
    euclidean real[],
    curvature FLOAT DEFAULT -1.0
)
RETURNS real[] AS $$
BEGIN
    RETURN ruvector_exp_map(ARRAY_FILL(0.0::real, ARRAY[array_length(euclidean, 1)]), euclidean, curvature);
END;
$$ LANGUAGE plpgsql IMMUTABLE;

-- Poincaré distance (geodesic)
CREATE OR REPLACE FUNCTION claude_flow.poincare_distance(
    x real[],
    y real[],
    curvature FLOAT DEFAULT -1.0
)
RETURNS FLOAT AS $$
BEGIN
    RETURN ruvector_poincare_distance(x, y, curvature);
END;
$$ LANGUAGE plpgsql IMMUTABLE;

-- Hyperbolic search in Poincaré ball
CREATE OR REPLACE FUNCTION claude_flow.hyperbolic_search(
    query ruvector(384),
    limit_count INT DEFAULT 10,
    curvature FLOAT DEFAULT -1.0
)
RETURNS TABLE (
    id UUID,
    content TEXT,
    euclidean_dist FLOAT,
    hyperbolic_dist FLOAT,
    hierarchy_level INT,
    metadata JSONB
) AS $$
DECLARE
    query_arr real[];
    query_poincare real[];
BEGIN
    -- Convert query to array and then to Poincaré
    SELECT array_agg(x::real ORDER BY ordinality) INTO query_arr
    FROM unnest(string_to_array(trim(both '[]' from query::text), ',')) WITH ORDINALITY AS t(x, ordinality);

    query_poincare := claude_flow.to_poincare(query_arr, curvature);

    RETURN QUERY
    SELECT
        he.id,
        he.content,
        (he.euclidean_embedding <-> query)::FLOAT AS euc_dist,
        COALESCE(ruvector_poincare_distance(he.poincare_embedding, query_poincare, curvature), 999.0)::FLOAT AS hyp_dist,
        he.hierarchy_level,
        he.metadata
    FROM claude_flow.hyperbolic_embeddings he
    WHERE he.euclidean_embedding IS NOT NULL
    ORDER BY he.euclidean_embedding <-> query
    LIMIT limit_count;
END;
$$ LANGUAGE plpgsql STABLE;

-- ============================================
-- PART 6: UTILITY FUNCTIONS
-- ============================================

-- Get RuVector version info
CREATE OR REPLACE FUNCTION claude_flow.ruvector_info()
RETURNS TABLE (
    version TEXT,
    simd_info TEXT
) AS $$
BEGIN
    RETURN QUERY
    SELECT ruvector_version(), ruvector_simd_info();
END;
$$ LANGUAGE plpgsql STABLE;

-- Cosine similarity helper (converts cosine distance to similarity)
CREATE OR REPLACE FUNCTION claude_flow.cosine_similarity(
    a ruvector,
    b ruvector
)
RETURNS FLOAT AS $$
BEGIN
    RETURN (1 - (a <=> b))::FLOAT;
END;
$$ LANGUAGE plpgsql IMMUTABLE;

-- L2 distance helper
CREATE OR REPLACE FUNCTION claude_flow.l2_distance(
    a ruvector,
    b ruvector
)
RETURNS FLOAT AS $$
BEGIN
    RETURN (a <-> b)::FLOAT;
END;
$$ LANGUAGE plpgsql IMMUTABLE;

-- Upsert memory entry
CREATE OR REPLACE FUNCTION claude_flow.upsert_memory(
    p_key VARCHAR(255),
    p_value TEXT,
    p_embedding ruvector(384) DEFAULT NULL,
    p_namespace VARCHAR(100) DEFAULT 'default',
    p_metadata JSONB DEFAULT '{}',
    p_ttl TIMESTAMPTZ DEFAULT NULL
)
RETURNS UUID AS $$
DECLARE
    v_id UUID;
BEGIN
    INSERT INTO claude_flow.memory_entries (key, value, embedding, namespace, metadata, ttl, updated_at)
    VALUES (p_key, p_value, p_embedding, p_namespace, p_metadata, p_ttl, NOW())
    ON CONFLICT (key, namespace) DO UPDATE SET
        value = EXCLUDED.value,
        embedding = COALESCE(EXCLUDED.embedding, claude_flow.memory_entries.embedding),
        metadata = EXCLUDED.metadata,
        ttl = EXCLUDED.ttl,
        updated_at = NOW()
    RETURNING id INTO v_id;

    RETURN v_id;
END;
$$ LANGUAGE plpgsql;

-- ============================================
-- COMPLETION
-- ============================================

DO $$
DECLARE
    v_version TEXT;
    v_simd TEXT;
BEGIN
    SELECT ruvector_version() INTO v_version;
    SELECT ruvector_simd_info() INTO v_simd;

    RAISE NOTICE '';
    RAISE NOTICE '============================================';
    RAISE NOTICE 'RuVector PostgreSQL Initialization Complete!';
    RAISE NOTICE '============================================';
    RAISE NOTICE '';
    RAISE NOTICE 'RuVector Version: %', v_version;
    RAISE NOTICE 'SIMD: %', v_simd;
    RAISE NOTICE '';
    RAISE NOTICE 'Schema: claude_flow';
    RAISE NOTICE 'Tables: embeddings, patterns, agents, trajectories,';
    RAISE NOTICE '        memory_entries, hyperbolic_embeddings,';
    RAISE NOTICE '        graph_nodes, graph_edges';
    RAISE NOTICE 'Indices: 6 HNSW indices + 3 B-tree indices';
    RAISE NOTICE '';
    RAISE NOTICE 'Key Functions:';
    RAISE NOTICE '  - claude_flow.search_similar(embedding, limit, min_sim)';
    RAISE NOTICE '  - claude_flow.search_memory(embedding, namespace, limit)';
    RAISE NOTICE '  - claude_flow.search_patterns(embedding, type, limit)';
    RAISE NOTICE '  - claude_flow.find_agents(embedding, type, limit)';
    RAISE NOTICE '  - claude_flow.hyperbolic_search(embedding, limit, curvature)';
    RAISE NOTICE '  - claude_flow.upsert_memory(key, value, embedding, namespace)';
    RAISE NOTICE '';
    RAISE NOTICE 'Operators: <=> (cosine), <-> (L2), <#> (neg inner product)';
    RAISE NOTICE '';
END $$;
