# Nodus Vision LAN Browser Access Design

## 1. Scope

This design adds trusted-LAN browser access to the existing Vision-owned HTTP provider without
changing payload ownership. Portal continues to discover descriptors through Pilot and reads
Camera payloads directly from Vision.

```text
Portal browser -- component and endpoint discovery --> Pilot
Portal browser -- health, metadata, MJPEG payloads ---> Vision
```

Pilot does not proxy Camera bytes. Portal and Pilot source are outside this change.

## 2. Configuration contract

`provider.bind_host`, `provider.port`, and `provider.advertised_base_url` remain separate:

- `bind_host` may be `0.0.0.0` for a trusted-LAN listener;
- `advertised_base_url` must contain a concrete client-reachable host and the configured port; and
- `0.0.0.0`, wildcard hosts, user information, query, and fragment remain invalid advertised
  values.

The provider configuration adds optional `allowed_origins`, a bounded unique array of exact HTTP
origins. An origin contains scheme, host, and explicit port only. Paths, wildcard origins,
credentials, queries, fragments, and control characters are rejected during configuration
parsing.

An omitted or empty list keeps browser cross-origin access disabled while allowing non-browser
clients that do not send an `Origin` header. The initial LAN example allows only the known local
Portal origins; it does not use `*`.

## 3. HTTP behavior

For a request without `Origin`, existing provider behavior is unchanged. For a request with
`Origin`:

- an exact configured origin is accepted;
- a non-configured origin receives a bounded `403 cors_origin_denied` response before route
  execution; and
- accepted normal and error responses include `Access-Control-Allow-Origin` with the exact origin
  and `Vary: Origin`.

An accepted browser preflight uses `OPTIONS` and returns `204` with:

- `Access-Control-Allow-Origin` for the exact requesting origin;
- `Access-Control-Allow-Methods: GET, POST, OPTIONS`;
- `Access-Control-Allow-Headers: Accept, Content-Type`;
- `Access-Control-Max-Age: 600`; and
- `Vary: Origin`.

MJPEG response headers receive the same exact-origin policy. No credentialed CORS, wildcard CORS,
authentication, TLS, or Internet exposure is claimed by this trusted-LAN phase.

## 4. Runtime and ownership

The parsed allowlist is immutable for the process lifetime and is passed into the existing bounded
Provider HTTP server. CORS decisions occur on the provider I/O thread before invoking route
callbacks. Capture, encoding, recording, Pilot lifecycle, and stream backpressure ownership do not
change.

The Pilot endpoint catalog continues to use `advertised_base_url`. Therefore a tablet receives a
LAN-reachable Vision address instead of interpreting `127.0.0.1` as the tablet itself.

## 5. Acceptance

- strict config accepts unique exact HTTP origins and rejects wildcard/path origins;
- a configured Origin receives CORS headers on JSON, error, and MJPEG responses;
- a configured preflight receives `204` and the bounded method/header policy;
- a disallowed Origin receives `403` without invoking the requested provider route;
- requests without Origin preserve current local native-client behavior;
- the Pilot-enabled fake example binds on `0.0.0.0` and advertises a concrete LAN address; and
- no physical Camera, Portal process, Pilot source, or external repository is executed or modified.
