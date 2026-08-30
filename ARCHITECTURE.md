# PrivaC Architecture & Deep Technical Specifications

This document outlines the internal mechanics, memory layouts, finite state machines, and threat model for PrivaC.

---

## 1. Threat Model & Privacy Guarantees

```
┌────────────────────────────────────────────────────────┐
│                   TRUST BOUNDARY                       │
│                                                        │
│  [Client Application / Agent Framework]               │
│                     ▲                                  │
│                     │  (Cleartext PII + Real Numbers)  │
│                     ▼                                  │
│  ┌──────────────────────────────────────────────────┐  │
│  │               PrivaC Reverse Proxy               │  │
│  │                                                  │  │
│  │   ┌───────────────────┐    ┌─────────────────┐   │  │
│  │   │  mlock() Vault    │    │  Arena Heap     │   │  │
│  │   │  (Protected RAM)  │    │  (Linear Bump)  │   │  │
│  │   └───────────────────┘    └─────────────────┘   │  │
│  └──────────────────────────────────────────────────┘  │
└─────────────────────────┬──────────────────────────────┘
                          │ (Blind Prompts, Algebraic Variables: VAR_1, Entity_A)
                          │ (NO PII, NO RAW FIGURES)
                          ▼
            [Public / Untrusted Remote LLM]
```

### Security Properties
1. **Zero-Knowledge Prompt Transmission:** Raw numerical figures and sensitive entity names are replaced before TCP egress.
2. **Deterministic Arithmetic Verification:** Eliminates LLM calculation hallucination by restricting the LLM to algebraic expression synthesis.
3. **Anti-Swapping (`mlock`):** Prevents kernel paging of sensitive session values to physical swap partitions.
4. **Defensive Zeroization:** Guaranteed memory wiping (`privac_secure_zero` with memory barriers) on session destruction.

---

## 2. Sliding-Window Delimiter FSM (Streaming SSE)

The primary challenge with streaming LLM proxying is handling tag delimiters split across unpredictable network chunks.

### State Transition Diagram
```
                     [PASSTHROUGH]
                      │         │
           c == '<'   │         │  c == 'E' or 'V' (Entity/VAR)
                      ▼         ▼
      [MATCHING_PREFIX]         [IN_ENTITY_TAG]
      (Buffers "<<calc:")       (Buffers until delimiter)
           │          │                 │
    Prefix │   Prefix │                 ▼
   Matches │ Mismatch │          [LOOKUP VAULT]
           ▼          ▼                 │
     [IN_CALC_EXPR] [FLUSH BUFFER]      ▼
     (Buffers formula)  │        [EMIT CLEARTEXT]
           │            ▼               │
    c == ">>"     [PASSTHROUGH]         ▼
           │                     [PASSTHROUGH]
           ▼
    [EVALUATE AST]
           │
           ▼
   [EMIT COMPUTED RESULT]
           │
           ▼
     [PASSTHROUGH]
```

---

## 3. Micro-Math AST VM

When `<<calc: (VAR_1 - VAR_2) * (1 - VAR_3)>>` is intercepted:
1. Variables are resolved from the active `session_vault_t`.
2. AST compilation via `tinyexpr` is executed.
3. The result is checked against `isnan()` and `isinf()` for division-by-zero safety.
4. Output is formatted with currency/precision rules inherited from the input variable metadata.
