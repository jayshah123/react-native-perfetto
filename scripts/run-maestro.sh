#!/usr/bin/env bash
set -euo pipefail

mkdir -p "$HOME/.maestro"

if command -v maestro >/dev/null 2>&1; then
  MAESTRO_BIN="$(command -v maestro)"
elif [ -x "$HOME/.maestro/bin/maestro" ]; then
  MAESTRO_BIN="$HOME/.maestro/bin/maestro"
else
  echo "Maestro CLI is not installed. Run: yarn maestro:install" >&2
  exit 1
fi

extract_app_id_from_flow() {
  local flow_file="$1"
  if [ ! -f "$flow_file" ]; then
    return 1
  fi

  awk -F ':' '
    $1 == "appId" {
      app = $2
      gsub(/^[[:space:]]+|[[:space:]]+$/, "", app)
      print app
      exit
    }
  ' "$flow_file"
}

find_ios_udid_with_app_installed() {
  local app_id="$1"
  local list_mode="${2:-available}"
  local udids=""
  local udid

  if [ -z "$app_id" ]; then
    return 1
  fi

  if [ "$list_mode" = "booted" ]; then
    udids="$(xcrun simctl list devices booted | awk -F '[()]' '/Booted/ { print $2 }')"
  else
    udids="$(xcrun simctl list devices available | awk -F '[()]' '/(Booted|Shutdown)/ { print $2 }')"
  fi

  while IFS= read -r udid; do
    if [ -z "$udid" ]; then
      continue
    fi

    if xcrun simctl get_app_container "$udid" "$app_id" data >/dev/null 2>&1; then
      echo "$udid"
      return 0
    fi
  done <<<"$udids"

  return 1
}

find_booted_ios_udid() {
  xcrun simctl list devices booted | awk -F '[()]' '/Booted/ { print $2; exit }'
}

boot_ios_simulator_if_needed() {
  local app_id="${1:-}"
  if ! command -v xcrun >/dev/null 2>&1; then
    echo "xcrun is required for iOS Maestro runs." >&2
    return 1
  fi

  local selected_udid=""
  local preferred_name

  if [ -n "$app_id" ]; then
    selected_udid="$(find_ios_udid_with_app_installed "$app_id" booted || true)"
  fi

  if [ -z "$selected_udid" ]; then
    selected_udid="$(find_booted_ios_udid)"
  fi

  if [ -n "$selected_udid" ]; then
    echo "$selected_udid"
    return 0
  fi

  if [ -n "$app_id" ]; then
    selected_udid="$(find_ios_udid_with_app_installed "$app_id" available || true)"
  fi

  if [ -z "$selected_udid" ] && [ -n "$app_id" ]; then
    echo "No simulator has app '$app_id' installed; falling back to first available simulator." >&2
  fi

  local candidate_udid="$selected_udid"
  preferred_name="${IOS_SIMULATOR_NAME:-}"

  if [ -z "$candidate_udid" ] && [ -n "$preferred_name" ]; then
    candidate_udid="$(
      xcrun simctl list devices available \
        | awk -v preferred="$preferred_name" -F '[()]' 'index($0, preferred) && /Shutdown/ { print $2; exit }'
    )"
  fi

  if [ -z "$candidate_udid" ]; then
    candidate_udid="$(
      xcrun simctl list devices available \
        | awk -F '[()]' '/iPhone/ && /Shutdown/ { print $2; exit }'
    )"
  fi

  if [ -z "$candidate_udid" ]; then
    candidate_udid="$(
      xcrun simctl list devices available \
        | awk -F '[()]' '/Shutdown/ { print $2; exit }'
    )"
  fi

  if [ -z "$candidate_udid" ]; then
    echo "No available iOS simulators found. Install a simulator runtime in Xcode." >&2
    return 1
  fi

  xcrun simctl boot "$candidate_udid" >/dev/null 2>&1 || true
  xcrun simctl bootstatus "$candidate_udid" -b >/dev/null
  open -a Simulator >/dev/null 2>&1 || true

  echo "$candidate_udid"
}

maestro_args=("$@")

if [ "${1:-}" = "test" ] && [ "${MAESTRO_PLATFORM:-}" = "ios" ]; then
  has_explicit_device_flag=false
  flow_path=""
  app_id=""

  for arg in "${maestro_args[@]}"; do
    case "$arg" in
      --udid | --device | --udid=* | --device=*)
        has_explicit_device_flag=true
        break
        ;;
      *.yaml | *.yml)
        if [ -z "$flow_path" ] && [ -f "$arg" ]; then
          flow_path="$arg"
        fi
        ;;
    esac
  done

  if [ -n "$flow_path" ]; then
    app_id="$(extract_app_id_from_flow "$flow_path" || true)"
  fi

  if [ "$has_explicit_device_flag" = false ]; then
    ios_udid="$(boot_ios_simulator_if_needed "$app_id")"

    if [ -n "$app_id" ] && ! xcrun simctl get_app_container "$ios_udid" "$app_id" data >/dev/null 2>&1; then
      echo "App '$app_id' is not installed on simulator '$ios_udid'." >&2
      echo "Install it first, for example:" >&2
      echo "  cd example && npx react-native run-ios --mode Release --udid $ios_udid --no-packager" >&2
      exit 1
    fi

    maestro_args=(--device "$ios_udid" "${maestro_args[@]}")
  fi
fi

exec "$MAESTRO_BIN" "${maestro_args[@]}"
