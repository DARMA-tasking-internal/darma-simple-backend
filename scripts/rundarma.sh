#!/bin/bash

if [ -z $1 ]; then
  echo "Syntax: $0 <executable> [args...]"
  exit 1
fi

COMMAND=$*

if [ -n "$COMMAND" ]; then
  echo "Executing: $COMMAND"
  $COMMAND
else
  # this shouldn't ever happen
  echo "ERROR: No command-line generated for this execution."
fi

