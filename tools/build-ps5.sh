#!/bin/bash
set -euo pipefail
: "${PS5_PAYLOAD_SDK:?Set PS5_PAYLOAD_SDK}"
cd "$(dirname "$0")/../ampr-ps5"
make -j4
