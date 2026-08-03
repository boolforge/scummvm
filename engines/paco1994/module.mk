MODULE := engines/paco1994

MODULE_OBJS = \
	ald_parser.o \
	detection.o \
	game_state.o \
	interaction.o \
	paco1994.o

MODULE_DIRS += \
	engines/paco1994

ifeq ($(ENABLE_PACO1994), DYNAMIC_PLUGIN)
PLUGIN := 1
endif

include $(srcdir)/rules.mk
