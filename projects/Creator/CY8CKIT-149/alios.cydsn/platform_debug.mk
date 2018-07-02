# Macros required for the generic driver for toolchain and/or operating system.

DESIGN= alios
CONFIG= debug
TOOLCHAIN_DIR ?= C:/Program Files (x86)/Cypress/PSoC Creator/4.2/PSoC Creator/import/gnu/arm/5.4.1/bin

CC_BASE= arm-none-eabi-gcc
CC_PATH= $(shell which $(CC_BASE) 2>/dev/null)
CC= $(if $(CC_PATH),$(CC_PATH),$(TOOLCHAIN_DIR)/$(CC_BASE))

OUT_DIR_CortexM0p= output/$(CONFIG)/CortexM0p
CFLAGS_CortexM0p= -mcpu=cortex-m0plus -mthumb -I. -IGenerated_Source/PSoC4 -Wa,-alh=$(OUT_DIR_CortexM0p)/$(basename $(<F)).lst -g -D DEBUG -D CY_CORE_ID=0 -Wall -ffunction-sections -ffat-lto-objects -Og

CDEPGEN_CortexM0p= -MM $< -MF $(OUT_DIR_CortexM0p)/$(<F).d -MT $(OUT_DIR_CortexM0p)/$(@F)  $(CFLAGS_CortexM0p)

LD_BASE= arm-none-eabi-gcc
LD_PATH= $(shell which $(LD_BASE) 2>/dev/null)
LD= $(if $(LD_PATH),$(LD_PATH),$(TOOLCHAIN_DIR)/$(LD_BASE))

LDFLAGS_CortexM0p= -mcpu=cortex-m0plus -mthumb -L Generated_Source/PSoC4 -Wl,-Map,$(OUT_DIR_CortexM0p)/$(DESIGN).map -T Generated_Source/PSoC4/cm0plusgcc.ld -specs=nano.specs -Wl,--gc-sections -g -ffunction-sections -Og -ffat-lto-objects

AS_BASE= arm-none-eabi-as
AS_PATH= $(shell which $(AS_BASE) 2>/dev/null)
AS= $(if $(AS_PATH),$(AS_PATH),$(TOOLCHAIN_DIR)/$(AS_BASE))

ASFLAGS_CortexM0p= -mcpu=cortex-m0plus -mthumb -I. -IGenerated_Source/PSoC4 -alh=$(OUT_DIR_CortexM0p)/$(basename $(<F)).lst -g -W

AR_BASE= arm-none-eabi-ar
AR_PATH= $(shell which $(AR_BASE) 2>/dev/null)
AR= $(if $(AR_PATH),$(AR_PATH),$(TOOLCHAIN_DIR)/$(AR_BASE))
ARFLAGS= -rs

RM= rm
RMFLAGS= -rf
