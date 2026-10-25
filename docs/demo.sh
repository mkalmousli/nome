#!/bin/bash
# Scripted demo recorded for the website:  HOME=$(mktemp -d) asciinema rec -c docs/demo.sh docs/demo.cast
# Commands are really executed; only the typing is simulated.
run() { printf '\033[32m$\033[0m '; for ((i = 0; i < ${#1}; i++)); do printf %s "${1:i:1}"; sleep 0.04; done
	sleep 0.4; printf '\n'; eval "$1"; sleep 1.2; }
mkdir -p ~/project && echo "hello from the host" > ~/project/notes.txt
clear
run "nome distros | head -5"
run "nome get alpine dev </dev/null"
run "nome dev cat /etc/os-release | head -2"
run "nome dev id -u"
run "nome mount dev ~/project"
run "nome dev cat /mnt/project/notes.txt"
run "nome cp dev dev-copy && nome ls"
run "nome dev apk add -q figlet && nome dev figlet nome"
run "nome rm dev-copy && nome rm dev"
