#!/usr/bin/env bash
# Usage: ./probe.sh [host:port]   (default m.tower1:11444)
B="http://${1:-m.tower1:11444}"
echo "== endpoint status =="
for ep in health slots metrics props; do
  printf 'GET /%-6s -> %s\n' "$ep" "$(curl -s -o /dev/null -w '%{http_code}' --max-time 4 "$B/$ep")"
done
echo
echo "== /slots: is_processing / n_decoded / n_remain =="
curl -s --max-time 4 "$B/slots" | grep -oE '"(is_processing|n_decoded|n_remain)":[^,}]*'
echo
echo "== /metrics: relevant gauges =="
curl -s --max-time 4 "$B/metrics" \
  | grep -E '^(llamacpp:predicted_tokens_seconds|llamacpp:prompt_tokens_seconds|llamacpp:requests_processing|llamacpp:tokens_predicted_total|llamacpp:tokens_predicted_seconds_total) '
