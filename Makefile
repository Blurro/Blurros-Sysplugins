# Plugin dev environment
PLUGIN_DEV_MESSAGE := Building boot.firm is disabled in this Nexus Sysplugin Dev environment, please run ./makeplugin.sh to build .3nx files.
.PHONY: all
.DEFAULT_GOAL := all

all:
	@printf '%s\n' '$(PLUGIN_DEV_MESSAGE)'

.DEFAULT:
	@printf '%s\n' '$(PLUGIN_DEV_MESSAGE)'