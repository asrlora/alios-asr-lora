# Generic GNU Make driver file.
OUT_DIR= output
CONFIG= debug
GEN_SOURCE_DIR= Generated_Source/PSoC4

include platform_$(CONFIG).mk
include app_source.mk
include $(GEN_SOURCE_DIR)/gen_source.mk

GEN_OBJECTS_CortexM0p= $(addprefix $(OUT_DIR_CortexM0p)/,$(notdir $(addsuffix .o,$(basename $(GEN_C_SOURCE_CortexM0p))))) $(addprefix $(OUT_DIR_CortexM0p)/,$(notdir $(addsuffix .o,$(basename $(GEN_ASM_SOURCE_CortexM0p))))) 
GEN_SEP_OBJECTS_CortexM0p= $(addprefix $(OUT_DIR_CortexM0p)/,$(notdir $(addsuffix .o,$(basename $(GEN_SEP_C_SOURCE_CortexM0p))))) 
DEPS_CortexM0p= $(addprefix $(OUT_DIR_CortexM0p)/,$(notdir $(addsuffix .c.d,$(basename $(APP_C_SOURCE_CortexM0p))))) $(addprefix $(OUT_DIR_CortexM0p)/,$(notdir $(addsuffix .c.d,$(basename $(GEN_C_SOURCE_CortexM0p))))) $(addprefix $(OUT_DIR_CortexM0p)/,$(notdir $(addsuffix .c.d,$(basename $(GEN_SEP_C_SOURCE_CortexM0p))))) 
APP_OBJECTS_CortexM0p= $(addprefix $(OUT_DIR_CortexM0p)/,$(notdir $(addsuffix .o,$(basename $(APP_C_SOURCE_CortexM0p))))) 

all : $(OUT_DIR_CortexM0p)/$(DESIGN).hex
prebuild_CortexM0p :
	Export/prebuild.sh

postbuild_CortexM0p= Export/postbuild.sh

$(OUT_DIR_CortexM0p)/$(DESIGN).hex : prebuild_CortexM0p $(OUT_DIR_CortexM0p)/$(DESIGN).elf
	$(postbuild_CortexM0p) $(OUT_DIR_CortexM0p)/$(DESIGN).elf $(OUT_DIR_CortexM0p)

$(OUT_DIR_CortexM0p)/$(DESIGN).elf : $(APP_OBJECTS_CortexM0p) $(GEN_SEP_OBJECTS_CortexM0p) $(OUT_DIR_CortexM0p)/$(DESIGN).a $(GEN_LIBS_CortexM0p) $(APP_LIBS_CortexM0p)
	"$(LD)" -Wl,--start-group $^ -o $@ $(LDFLAGS_CortexM0p) -Wl,--end-group

$(OUT_DIR_CortexM0p)/$(DESIGN).a : $(GEN_OBJECTS_CortexM0p)
	"$(AR)" $(ARFLAGS) $@ $^

$(OUT_DIR_CortexM0p)/%.o : $(GEN_SOURCE_DIR)/%.c
	@mkdir -p $(OUT_DIR_CortexM0p)
	@"$(CC)" $(CDEPGEN_CortexM0p)
	"$(CC)" -c $(CFLAGS_CortexM0p) $< -o $@

$(OUT_DIR_CortexM0p)/%.o : $(GEN_SOURCE_DIR)/%.s
	@mkdir -p $(OUT_DIR_CortexM0p)
	"$(AS)" $(ASFLAGS_CortexM0p) $< -o $@

$(OUT_DIR_CortexM0p)/%.o : %.c
	@mkdir -p $(OUT_DIR_CortexM0p)
	@"$(CC)" $(CDEPGEN_CortexM0p)
	"$(CC)" -c $(CFLAGS_CortexM0p) $< -o $@

clean:
	$(RM) $(RMFLAGS) $(OUT_DIR)
-include $(DEPS_CortexM0p)
