# IA-DEVS Reference Document

This document summarizes the theoretical background needed to implement the cadpya IA-DEVS simulator. It draws from the UA-DEVS paper (uadevs-paper.tex) and established DEVS literature.

## 1. Classic DEVS

### Atomic Model

A DEVS atomic model is a tuple `A = <X, Y, S, delta_int, delta_ext, lambda, ta>` where:

- **X**: set of inputs
- **Y**: set of outputs
- **S**: set of sequential states
- **Q = {(s, e) | s in S, 0 <= e <= ta(s)}**: total state set (s = state, e = elapsed time since last transition)
- **delta_int: S -> S**: internal transition function
- **delta_ext: Q x X -> S**: external transition function
- **lambda: S -> Y**: output function (executed *before* delta_int)
- **ta: S -> R+**: time advance function (infinity means passive/no scheduled event)

### Coupled Model

A DEVS coupled model is a tuple `C = <X, Y, D, M, I, Z, SELECT>` where:

- **X, Y**: input/output sets
- **D**: index set of sub-models
- **M = {M_d | d in D}**: the sub-models (atomic or coupled)
- **I_d**: influencer set for each d in D union {self}
- **Z_{i,d}**: translation functions between connected models
  - External input coupling: Z_{self,d}: X -> M_d.X
  - External output coupling: Z_{i,self}: M_i.Y -> Y
  - Internal coupling: Z_{i,d}: M_i.Y -> M_d.X
- **SELECT: P(D) - empty -> D**: tie-breaking function for simultaneous events

### Abstract Simulator (3 components)

1. **Simulator**: handles atomic models. Tracks `t_last`, `t_next`, `state`.
2. **Coordinator**: handles coupled models. Tracks `t_last`, `t_next`, and a tuple of child Engines (Simulators or Coordinators).
3. **Root Coordinator**: top-level loop that advances simulation time by calling the top Coordinator's *-function until passive.

### Simulation Messages (classic distributed form)

- **init**: initialize state and time
- ***-message**: trigger internal transition (output then state change)
- **x-message**: deliver external event
- **y-message**: route output from child to parent/siblings

The paper uses a **sequential** version where y-messages are embedded into * and x functions (no separate y-function), following the approach from Vicino's sequential PDEVS algorithms.

### Key Properties

- **S can be uncountable** (e.g., R+). Legitimacy only requires discrete events in any single simulation timeline.
- **X and Y can also be uncountable**.
- **Closure under coupling**: any coupled DEVS model is equivalent to an atomic DEVS model.
- **Set theory**: the paper uses ZFC (Zermelo-Fraenkel with Choice), which guarantees the Well-Ordering Theorem — every set has a total order.

## 2. PDEVS (Parallel DEVS)

PDEVS differs from classic DEVS in key ways (relevant because Cadmium implements PDEVS, not classic DEVS):

- **Bag of inputs**: delta_ext receives a bag (multiset) of simultaneous inputs, not one at a time.
- **Confluent transition (delta_con)**: handles the case when internal and external events are simultaneous, replacing SELECT.
- **No SELECT function**: all imminent components execute in parallel; conflicts resolved by delta_con.
- **Two-phase cycle**: (1) collect outputs from all imminent models, (2) execute transitions.

**IA-DEVS extends classic DEVS with SELECT**, not PDEVS. This is a deliberate choice to keep algorithms simpler for describing uncertainty propagation. The paper recommends extending to support ports for practical implementation.

## 3. UA-DEVS (Uncertainty-Aware DEVS)

UA-DEVS extends DEVS to capture uncertainty in model specifications. It cannot generally be simulated by finite means.

### Atomic Model

Same tuple structure as DEVS, but functions operate on **power sets**:

- **X_p = P(X) - empty**: uncertainty-aware inputs (sets of possible input values)
- **Y_p = P(Y)**: uncertainty-aware outputs (empty set = no output)
- **S_p = P(S) - empty**: uncertainty-aware states
- **Q_p = P(Q) - empty**: uncertainty-aware total states
- **delta_int: S_p -> S_p**
- **delta_ext: Q_p x X_p -> S_p**
- **lambda: S_p -> Y_p**
- **ta: S_p -> P(R+)** (empty set = passive, replaces infinity)

Key insight: each element of S_p, X_p, etc. is a **set of possible values** representing uncertainty. The functions map sets-of-sets to sets-of-sets.

### Coupled Model

Identical to DEVS coupled model — no changes needed. Sub-models are UA-DEVS models, and uncertainty propagates through the same coupling structure.

### Passiveness Convention

In UA-DEVS, passive state is represented by **ta returning empty set** (not infinity as in DEVS). This simplifies set operations: empty_set union {1s} = {1s}.

## 4. IA-DEVS (Interval-Approximated DEVS)

IA-DEVS approximates UA-DEVS by constraining uncertainty sets to **intervals**. An interval is compactly represented by (lower_bound, upper_bound, lower_closed, upper_closed) — finite storage for potentially infinite sets.

### Atomic Model

Tuple: `<X, Y, S, timebounds, delta_int, delta_ext, lambda, ta>` where:

- **X = <X_values, X_order, X_lowerbound, X_upperbound>**: values + order function + bounding functions
- **Y = <Y_values, Y_order, Y_lowerbound, Y_upperbound>**: same pattern
- **S = <S_values, S_order, S_lowerbound, S_upperbound>**: same pattern
- **timebounds = <T_lowerbound, T_upperbound>**: bounding functions for time approximation

The base functions (delta_int, delta_ext, lambda, ta) are defined identically to UA-DEVS. From these, **approximated functions** are mechanically derived:

- **Delta_int(s) = (S_lowerbound(delta_int(s)), S_upperbound(delta_int(s))) + delta_int(s)**
- **Delta_ext(q, x) = (S_lowerbound(delta_ext(q, x)), S_upperbound(delta_ext(q, x))) + delta_ext(q, x)**
- **Lambda(s) = (Y_lowerbound(lambda(s)), Y_upperbound(lambda(s))) + lambda(s)**
- **TA(s) = empty if ta(s)=empty, else (T_lowerbound(ta(s)), T_upperbound(ta(s))) + ta(s)**

The `+ original_set` notation means: include the original set's boundary points in the interval (close the interval at those points if they were in the set).

### Interval Sets

- **X_I**: intervals over X_values using X_order, with bounds in image sets of X_lowerbound/X_upperbound
- **Y_I, S_I**: analogous
- **R+_I**: intervals over R+ with bounds from timebounds functions
- **Q_I**: pairs of intervals (S_I, R+_I)

### Coupled Model

Tuple: `C = <X, Y, D, M, Z, SELECT>` where:

- X and Y are quadruples (values, order, lowerbound, upperbound) as in atomic models
- Translation functions Z_{i,d} defined on X_p/Y_p as in UA-DEVS
- Approximated translations Z^AI_{i,d} defined on X_I/Y_I using the receiving model's bound functions

### Approximation Strategy

Different choices of order and bounding functions produce different approximations:
- More precise bounds = less spurious trajectories but more expensive computation
- Coarser bounds (e.g., constants like -inf, +inf) = cheaper but more uncertainty inflation
- Examples from paper: rounding to fixed-point (floor/ceil at 1/100 or 1/1000), min/max for discrete sets, constant infinity for unused inputs

### Key Guarantee

IA-DEVS simulation always produces a **superset** of UA-DEVS trajectories. It may include unreachable trajectories but never misses a reachable one.

## 4b. Cadmia C++ Implementation Patterns

The Cadmia C++ codebase (`cadmia/` directory, to be removed later) provides concrete patterns for IA-DEVS model implementation. Key observations:

### Interval Design

`interval<T>` is generic over any totally-ordered, default-initializable type T. Attributes:
- `lower`, `upper` (values of type T)
- `lower_closed`, `upper_closed` (booleans)
- `lower_inf_sign`, `upper_inf_sign` (int: -1, 0, or +1)

Factory methods: `closed(lo, hi)`, `right_open(lo, hi)`, `left_open(lo, hi)`, `open(lo, hi)` — each with infinity overloads. `empty_interval()` returns `open(T{}, T{})`.

Operations: Minkowski addition (`+`), interval subtraction (`-`), `is_subset_of`, `intersects`, `is_empty`. Overflow policies handle int and decimal types, promoting to infinity on overflow.

### Decimal Fixed-Point Type

`decimal<Scale, Raw>` provides exact fixed-point arithmetic. E.g., `decimal<3>` has 1ms resolution. Used for all time values to avoid floating-point issues. This is important — the paper's bounding functions (floor/ceil to 1/100, 1/1000) map naturally to fixed-point rather than float.

### Atomic Model API (C++ concept)

Models must provide:
- **Type aliases**: `state_t`, `time_t`, `input_t`, `output_t` (base types) and their interval counterparts `state_i_t`, `time_i_t`, `input_i_t`, `output_i_t`
- **Constructor**: takes `const state_i_t&` (initial state interval)
- **Methods** (mutate internal state interval):
  - `internal_transition()` → void
  - `external_transition(time_i_t elapsed, input_i_t x)` → void
  - `output() const` → `output_i_t`
  - `time_advance() const` → `time_i_t`
  - `state_interval() const` → `const state_i_t&`

Key: the model **owns and mutates its state interval**. Transition functions are void — they modify the internal `state_` member directly. This differs from the paper's functional notation but is equivalent.

### Composite State Pattern (Processor)

The Processor's `state_t` is a struct with `tocj: dec3` and `qj: deque<int>`. It implements `operator<=>` for total ordering: compare by tocj first, then by queue size, then element-by-element. The state interval is then `interval<state_t>` — the lower/upper bounds are full state_t values. Transition functions manipulate `state_.lower` and `state_.upper` independently to produce the new interval.

### Discrete Phase Pattern (Counter)

The Counter's `state_t` has `phase: enum {passive, output}` and `count: int`. The enum is ordered (passive < output). When the state interval spans phases (lower=passive, upper=output), time_advance returns `[0, +inf)` representing uncertainty about whether the model is active. This is the pattern for models with discrete mode changes under uncertainty.

### No Ports

Cadmia uses **no port abstraction**. Each atomic model has a single typed input and single typed output. Coupling connects models by name with translator functions. This matches the paper's recommendation to keep the base formalism port-free.

## 5. IA-DEVS Simulation Algorithms

### Simulator (for Atomic models)

Internal variables: `t_last` (R+_I), `t_next` (R+_I), `state` (S_I)

**init(q, t)**:
- state = q.state
- t_last = t - q.time
- t_next = t_last + TA(state)

***-function(t) -> Optional<Y>**:
- Assert t subset_of t_next
- y = Lambda(state)
- state = Delta_int(state)
- t_last = t
- t_next = t_last + TA(state)
- return Optional.of(y)

**x-function(x, t)**:
- Compute t_local (elapsed time interval) with special handling when t overlaps t_last (lower = 0 in that case)
- state = Delta_ext(<state, t_local>, x)
- t_last = t
- t_next = t_last + TA(state)

### Coordinator (for Coupled models)

Internal variables: `t_last` (R+_I), `t_next` (R+_I), `E` (tuple of child Engines)

**init(q, t)**:
- Create Engine (Simulator or Coordinator) for each sub-model
- Forward init to each with corresponding q element and t
- BoundTs()

**BoundTs()** (paper's abstract description):
- t_last = interval from max of all children's t_last lower/upper ends
- t_next = interval from min of all children's t_next lower/upper ends
- Open/closed boundaries determined by existence conditions on children

**Implementation note**: BoundTs as described in the paper has no direct computable representation in isolation. In practice, the bounding logic must be coupled to the coupled model's Z translation functions — the bounds are applied as part of the translation/routing step, not as a standalone function.

***-function(t) -> Optional<Y>**:
This is the most complex algorithm. Key steps:
1. Find **imminent engines**: those whose t_next overlaps with t
2. Sort imminents by `<<` ordering (by lowest value, then by smallest range)
3. Compute **limit**: the interval of time that can be safely advanced in one step
4. **If limit is punctual** (single value): use SELECT to choose which engine advances. If the punctual limit is strictly contained in the engine's t_next, FORK (branch) for the case where this point is excluded.
5. **If limit is non-punctual**: FORK for each imminent engine (each branch advances a different engine first)
6. In each branch: call route() to execute the imminent's *-function and deliver outputs via Z^AI and x-function to influenced engines
7. BoundTs()

**x-function(x, t)**:
- Route x to influenced sub-models using Z^AI_{self,i}
- Call x-function on each influenced engine
- BoundTs()

**route(engine, t) -> Optional<Y>**:
- y = engine.*-function(t)
- For each influenced engine r: E_r.x-function(Z^AI_{i,r}(y), t)
- If self is influenced: return y, else return nil

### Root Coordinator

**simulate(coupled_model c, init_state q, time t)**:
- E = Coordinator(c)
- E.init(q, t)
- t_current = E.t_next
- While t_current != empty:
  - E.*-function(t_current)
  - t_current = E.t_next

## 6. Branching (FORK) Mechanism

The IA-DEVS simulator produces an **infinite tree** of branches. Each branch represents a possible trajectory. Branching occurs when:

1. **Non-punctual limit with multiple imminents**: Different orderings of simultaneous events create different branches.
2. **Punctual limit strictly inside an engine's t_next**: The simulation branches for the case where the event happens at exactly that point vs. not.

From the case study: 4 generators producing simultaneous events create 4! = 24 branches in the first wave. Many branches may have identical variable values and could be merged to avoid redundant computation.

### Implementation Strategy (decided)

**BFS execution queue**, not actual process forking or DFS:

- The simulation maintains a **queue of pending steps**. Initially, the queue contains a single "next step".
- When a step produces a **single** next step, it is appended to the queue.
- When a step produces **multiple** next steps (branching), the entire simulation state (model hierarchy) is **cloned** for each branch, and all resulting next steps are queued.
- This produces a **breadth-first exploration** of the branching tree. DFS is not viable because the tree is infinite (simulation never terminates along any single branch unless all models become passive).
- Memory optimization (DB-backed state, caching, deduplication) is deferred to later phases. The initial implementation will hold all cloned states in memory, targeting small models first.

## 7. Differences Between DEVS, PDEVS, and IA-DEVS

| Feature | Classic DEVS | PDEVS | IA-DEVS |
|---------|-------------|-------|---------|
| Simultaneous events | SELECT function | Confluent transition | SELECT + branching |
| Input handling | Single input | Bag of inputs | Single input (interval) |
| State representation | Single value | Single value | Interval |
| Time representation | Single R+ value | Single R+ value | Interval of R+ |
| Output | Single value | Single value | Interval |
| Passiveness | ta = infinity | ta = infinity | ta = empty set |
| Ports | Optional extension | Standard | Not in base spec (recommended for implementation) |

## 8. Cadmium (C++ PDEVS Simulator) Architecture

Cadmium (github.com/sdavtaker/cadmium) is a C++17 header-only PDEVS simulator with these design characteristics:

- **Strong typing**: compile-time model validation via templates
- **Typed ports**: messages are typed through port definitions
- **Modular architecture**: pluggable scheduler, tracer, termination conditions
- **Single-threaded by default**: sequential call/return message passing (optional Boost.Thread concurrency)
- **Header-only**: no separate compilation needed

Key structural elements:
- Atomic models define ports, state struct, and transition functions as class methods
- Coupled models define component instantiation and port couplings
- Simulator/Coordinator/Root Coordinator hierarchy mirrors DEVS abstract simulator
- JSON support (nlohmann/json) for serialization
- Boost.Test for testing

## 9. Computational Considerations for cadpya

### Key design decision: computable functions only

The paper decomposes IA-DEVS model functions into base functions (delta_int, delta_ext, lambda, ta) plus bounding/order functions, then mechanically derives the approximated functions (Delta_int, Delta_ext, Lambda, TA). This decomposition exists for **theoretical justification** — proving that approximation always produces a superset of trajectories.

In the implementation, **modelers directly implement the approximated (capital-letter) functions**: Delta_int, Delta_ext, Lambda, TA. These functions take interval inputs and produce interval outputs, with the bounding already incorporated. There is no separate `delta_int` + `S_lowerbound` composition at runtime. The small-letter functions may not be individually computable, but their composition with bounds (the capital-letter functions) is.

This means the atomic model API exposes:
- **Delta_int(state_interval) -> state_interval**
- **Delta_ext(total_state_interval, input_interval) -> state_interval**
- **Lambda(state_interval) -> output_interval**
- **TA(state_interval) -> time_interval or empty**

### What translates directly from the paper
- Atomic model tuple structure (with the simplification above)
- Coupled model tuple structure
- Simulator, Coordinator, Root Coordinator algorithms
- BoundTs logic
- The init, *-function, x-function interfaces

### What needs adaptation for implementation
- **Intervals**: need a proper Interval data type with open/closed bounds, arithmetic operations (addition, subtraction), set operations (intersection, union), and comparison
- **FORK/branching**: BFS execution queue with state cloning (see Section 6)
- **Infinity representation**: time intervals need support for infinity (positive and negative)
- **Passiveness**: empty set / None for TA result (not infinity)
- **State representation**: S_I elements can be complex (e.g., pairs of intervals for the Processor model). Order/bound logic is embedded in the model's capital-letter functions.
- **Branch merging**: deferred to later optimization phase
- **Ports**: out of scope. No research on how port-based coupling (EIC/IC/EOC) translates to uncertainty intervals. We use Z translation functions directly.

## 10. Simulation Output Format

The simulator produces a **structured log** — each line is a parseable record (JSON) that enables:
- **Trace-back**: following `parent_step` references to reconstruct how a state was reached
- **Graph construction**: the `branch` + `parent_step` fields form a tree/DAG of all explored simulation paths
- **Querying**: filtering by component, operation, time range, or branch

Each log entry contains:

| Field | Description |
|-------|-------------|
| `step` | Monotonic step counter (unique within a branch) |
| `branch` | Branch identifier |
| `parent_step` | Step that produced this one (null for init) |
| `time` | Global simulation time interval at this step |
| `component` | Name of the model being acted on |
| `operation` | One of: `init`, `internal`, `external`, `output`, `fork` |
| `state` | State interval after the operation |
| `output` | Output interval (for internal/output operations) |
| `input` | Input interval (for external operations) |
| `t_next` | Next scheduled event time interval |

The log is the primary output artifact of a simulation run. Visualization and analysis tools consume it downstream.
