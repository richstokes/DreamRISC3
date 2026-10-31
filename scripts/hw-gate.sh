#!/bin/bash
# Wait until no other session is using the console, then run the given command.
# Every hardware run goes through this, because dc-hw power cycles the console.
until ! ps -axo command | grep -E '[d]c-hw\.sh (run|power|reset)|[d]c-tool-ip' | grep -v hw-gate -q; do
  sleep 5
done
exec "$@"
