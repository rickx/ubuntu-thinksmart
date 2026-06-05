# USB Network Access (2026-06-05)

## Active management endpoint

The device is reachable over USB networking at:

- `user@172.16.42.1`

This path was used for Wi-Fi KCM link debugging and deployment in this session.

## Why this matters

When Wi-Fi UI or network policy is under active modification, USB networking provides a safer control path that avoids lockout risk from Wi-Fi regressions.

## Practical usage

```bash
ssh user@172.16.42.1
```

Use this endpoint for on-device patching, service restarts, log collection, and rollback steps.
