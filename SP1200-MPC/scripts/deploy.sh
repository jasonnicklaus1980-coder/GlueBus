#!/usr/bin/env bash
# Install SP1200 on an MPC over SSH (same procedure as the JV-880 and GlueBus packages):
#   ./scripts/deploy.sh <mpc-ip> [user]        # default user: root
#   YES=1 ./scripts/deploy.sh <mpc-ip>         # skip the on-device confirmation
# Builds the release folder first if dist/ is missing. Needs root SSH access to the MPC.
set -euo pipefail
cd "$(dirname "$0")/.."
HOST="${1:?usage: deploy.sh <mpc-ip> [user]}"; USER_="${2:-root}"; NAME=SP1200-2.0.0
[[ -d dist/$NAME ]] || ./scripts/package.sh
# tar over ssh: works even if the MPC has no scp/sftp-server
tar -C dist -cf - "$NAME" | ssh "$USER_@$HOST" "rm -rf /tmp/$NAME && tar -C /tmp -xf -"
if [[ "${YES:-0}" == 1 ]]; then ssh "$USER_@$HOST" "sh /tmp/$NAME/install.sh -y"
else ssh -t "$USER_@$HOST" "sh /tmp/$NAME/install.sh"; fi
