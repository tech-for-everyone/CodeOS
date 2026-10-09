#!/bin/bash
# CodeOS color helpers — borrowed from AluminiumOS patterns

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
BOLD='\033[1m'
RESET='\033[0m'

error()   { printf "${RED}%s${RESET}\n" "$1"; }
success() { printf "${GREEN}%s${RESET}\n" "$1"; }
info()    { printf "${YELLOW}%s${RESET}\n" "$1"; }
warn()    { printf "${YELLOW}warning: %s${RESET}\n" "$1"; }
step()    { printf "\n${CYAN}${BOLD}==> %s${RESET}\n" "$1"; }
