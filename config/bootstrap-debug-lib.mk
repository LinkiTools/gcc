# Stage1 compiler may not support this flag.
# STAGE1_CFLAGS += -fcompare-debug=-gtoggle
STAGE2_CFLAGS += -gtoggle -fcompare-debug=
STAGE3_CFLAGS += -fcompare-debug=-gtoggle
STAGE4_CFLAGS += -fcompare-debug=-fvar-tracking-assignments-toggle
# This might be enough after testing:
# TFLAGS += -fcompare-debug=-g0
# Don't use -gtoggle for target libs, this breaks crtstuff on ppc.
STAGE1_TFLAGS += -fcompare-debug=
STAGE2_TFLAGS += -fcompare-debug=-fvar-tracking-assignments-toggle
STAGE3_TFLAGS += -fcompare-debug=-g0
STAGE4_TFLAGS += -fcompare-debug=-fvar-tracking-assignments-toggle
do-compare = $(SHELL) $(srcdir)/contrib/compare-debug $$f1 $$f2
