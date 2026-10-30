# Prune generated build trees before collecting app sources or dependencies.
find_app_sources = find $(1) -type d \( -name build -o -name Build -o -name DerivedData -o -name '*.xcodeproj' -o -name '*.xcworkspace' \) -prune -o -type f $(2) -print
app_unity_sources = $(call find_app_sources,$(1),-name '*.c' ! -name main.c ! -path '*/components/*' ! -path '*/tests/*' ! -path '*/reel/*') | sort
