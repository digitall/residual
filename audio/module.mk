MODULE := audio

MODULE_OBJS := \
	audiostream.o \
	fmopl.o \
	mididrv.o \
	midiparser.o \
	mixer.o \
	mpu401.o \
	musicplugin.o \
	null.o \
	timestamp.o \
	decoders/flac.o \
	decoders/mp3.o \
	decoders/raw.o \
	decoders/vorbis.o \
	decoders/xa.o \
	softsynth/adlib.o \
	softsynth/cms.o \
	softsynth/opl/dbopl.o \
	softsynth/opl/dosbox.o \
	softsynth/opl/mame.o \
	softsynth/fluidsynth.o \
	softsynth/mt32.o 

ifndef USE_ARM_SOUND_ASM
MODULE_OBJS += \
	rate.o
else
MODULE_OBJS += \
	rate_arm.o \
	rate_arm_asm.o
endif

# Include common rules
include $(srcdir)/rules.mk
