# Reticulum Service Milestone 1

This milestone lands `ReticulumService` as a first-class Tactility system service.

The goal of this milestone is not to ship a temporary "minimal endpoint" implementation. The goal is to establish the final-shaped service boundary that future `Chat`, `Contacts`, `Weather`, `App Store`, and external apps can build on without forcing a structural rewrite later.

## Goals

- Register `ReticulumService` as a primary system service
- Keep Reticulum bearer-agnostic at the service core
- Separate interface adaptation from transport state and app-facing APIs
- Provide stable public types for identities, destinations, links, resources, paths, interfaces, and events
- Create the persistent directory layout that the final service will own
- Provide at least one real interface adapter entry point for end-to-end service integration

## Non-Goals

- Full protocol compatibility with the Python Reticulum reference implementation
- Complete packet parsing, forwarding, announce validation, or cryptographic identity binding
- A finished LXMF, NomadNet, or application-level protocol implementation
- Final path selection, retry, retransmission, or multi-interface scheduling heuristics

## Why This Is a Full-Service Milestone

Reticulum is intended to be shared infrastructure, not an app-local networking helper. In Tactility, that means it belongs in the system service layer next to other shared capabilities.

Milestone 1 therefore lands the final module boundaries up front:

- `ReticulumService`
- `IdentityStore`
- `DestinationRegistry`
- `PacketCodec`
- `TransportCore`
- `LinkManager`
- `ResourceManager`
- `InterfaceManager`
- interface adapters under `service/reticulum/interfaces`

This avoids the common failure mode where a temporary "simple" implementation lets apps bind directly to one bearer such as `ESP-NOW`, and later forces a rewrite when links, resources, multi-hop routing, or a second bearer such as LoRa are introduced.

## Service Placement

`ReticulumService` is registered as a primary system service during Tactility startup. This gives it the same lifecycle as the other core services and lets it initialize before applications begin to depend on it.

The service is designed so that:

- apps do not talk directly to `ESP-NOW` or LoRa
- interface-specific details stay below the service boundary
- app protocols such as chat, contacts discovery, weather requests, and store downloads stay above the service boundary

## Current Module Responsibilities

### `ReticulumService`

Owns the runtime lifecycle, persistence layout, dispatcher thread, interface registration, and system-wide Reticulum event publishing.

### `IdentityStore`

Owns persisted identity-related state. In this milestone it provides a provisional bootstrap identity source and directory ownership so later cryptographic identity material has a stable home.

### `DestinationRegistry`

Owns local destination registration and lookup. In this milestone it derives provisional destination hashes from the bootstrap identity and destination name.

### `PacketCodec`

Owns packet decoding and encoding concerns. In this milestone it only extracts an outer packet summary so the service can observe ingress and drive future protocol state machines without forcing apps or interfaces to parse raw bytes.

### `TransportCore`

Owns transport-visible path state, including interface association and next-hop data. In this milestone it stores path entries and exposes the path table to the rest of the service.

### `LinkManager`

Owns link state records and negotiated link properties.

### `ResourceManager`

Owns resource transfer state records and progress tracking.

### `InterfaceManager`

Owns interface registration, startup, teardown, and outbound frame dispatch.

### `EspNowInterface`

Provides the first real bearer adapter by bridging the existing Tactility `ESP-NOW` service into the new Reticulum service boundary.

### `LoRaInterface`

Exists as a placeholder adapter so the service boundary is explicitly multi-bearer from day one, even though a hardware-backed LoRa transport has not yet been implemented in this repository.

## Public API Surface

The public headers under `Tactility/Include/Tactility/service/reticulum/` intentionally expose the stable service-facing model rather than protocol internals:

- `Types.h`
- `Events.h`
- `Interface.h`
- `Reticulum.h`

This API is meant for internal apps first, and can later be mirrored into `TactilityC` once the higher-level application contract is ready for external ELF apps.

## Persistence Layout

On startup the service creates and owns these directories in its user data root:

- `identities`
- `ratchets`
- `paths`
- `links`
- `resources`
- `interfaces`
- `cache`

The intent is to reserve the final ownership boundaries now so future implementation work can deepen these stores instead of relocating them later.

## Event Model

The service publishes Reticulum-wide events through `PubSub<ReticulumEvent>`.

At this milestone the event stream is primarily for lifecycle and observability:

- runtime state changes
- interface attach and detach
- interface start and stop
- local destination registration
- inbound frame queueing
- packet envelope observation
- service-level errors

This is enough for service integration, diagnostics, and future app protocol bindings without prematurely freezing application-specific semantics.

## Multi-Bearer Model

The service core is bearer-agnostic. Bearer coordination is handled below the app layer and above the physical bearer-specific implementation.

That means:

- one Reticulum service can host multiple interfaces
- a path entry records which interface learned or owns the next hop
- app code should target destinations, links, requests, and resources rather than manually choosing `ESP-NOW` or LoRa
- future path selection can evolve without changing app-facing contracts

## App Integration Model

This milestone does not yet convert `Chat`, `Contacts`, `Weather`, or `App Store`, but it does define how they should connect later:

- `Contacts` should subscribe to announce and path-related service events, and manage alias and trust metadata locally
- `Chat` should talk to a protocol client above Reticulum rather than directly owning a bearer
- `Weather` should map naturally onto request and response flows
- `App Store` should use request and resource flows for manifests and package transfer, while installation remains a separate app-layer concern

## Provisional Elements

Several parts of the current implementation are intentionally marked provisional. They exist to complete the milestone's architectural landing, not to pretend the wire protocol is already finished.

- bootstrap identity material is placeholder state, not final Reticulum key management
- destination hashes are provisional, not final protocol-derived hashes
- packet parsing is currently envelope-level summarization only
- path, link, and resource managers currently provide state ownership rather than full protocol behavior
- `LoRaInterface` is a structural placeholder pending a real hardware backend

These are acceptable in Milestone 1 because the service architecture, ownership boundaries, and public contracts are the actual milestone deliverable.

## Exit Criteria

Milestone 1 is complete when all of the following are true:

- `ReticulumService` is registered and started by the system
- the service owns a stable persistence layout
- the public service API compiles cleanly inside the codebase
- interface adapters can be registered through the service boundary
- at least one real interface adapter is wired through the service
- apps have a stable service-facing API to build against next

## Next Steps After This Milestone

- replace provisional bootstrap identity logic with protocol-faithful identity and destination derivation
- implement Reticulum packet parsing and encoding beyond envelope summaries
- add announce, proof, and path request state handling
- implement link establishment and maintenance state machines
- implement request, response, and resource transfer machinery
- add a real LoRa-backed interface adapter
- begin migrating application protocols onto the service
