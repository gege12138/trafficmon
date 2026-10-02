# SMB CSV archive upload

`trafficupload` uploads one existing Phase 2B CSV archive without changing its format. It writes
to a unique temporary remote name, downloads that temporary object for a streaming byte comparison,
and only then makes the final name visible with a server-side `rename`. A failed upload or failed
verification never renames the temporary object and triggers a best-effort cleanup.

The command reads the username and password as two lines from standard input. Credentials are kept
out of process arguments and are written only to a mode-0600 temporary authentication file, which is
removed before the command returns. Passwords are never included in diagnostics.

```text
printf '%s\n%s\n' "$username" "$password" | \
    trafficupload --host nas.example --share archive --remote-dir trafficmon \
    --file trafficmon-2026-09.csv --credentials-stdin
```

OpenWrt provides the explicit `trafficupload-uci FILE` wrapper. Its `config smb 'archive'` section is
disabled by default and contains `enabled`, `host`, `share`, `username`, `password`, and `remote_dir`.
This phase does not schedule or automatically invoke uploads.

Install the optional client before enabling the feature:

```sh
opkg update
opkg install samba4-client
```

The dependency is optional because SMB upload is disabled by default. If the client is absent, an
explicit upload fails safely with `smbclient is unavailable`; the daemon remains unaffected.
