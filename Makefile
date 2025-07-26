OBJ_DIR := build
ISO_DIR := iso
INCLUDE := -I include

SRCS := $(shell find . -name '*.c' -o -name '*.S' -o -name '*.asm' | sed 's|^./||')
OBJS := $(patsubst %.c, $(OBJ_DIR)/%.o, $(filter %.c,$(SRCS))) \
        $(patsubst %.S, $(OBJ_DIR)/%.o, $(filter %.S,$(SRCS))) \
        $(patsubst %.asm, $(OBJ_DIR)/%.o, $(filter %.asm,$(SRCS)))
OBJS := $(filter-out $(OBJ_DIR)/boot/boot.o, $(OBJS))

CC := x86_64-elf-gcc-15.1.0
AS := x86_64-elf-as
NASM := nasm
LD := x86_64-elf-ld
CFLAGS := -ffreestanding  -Wall -Wextra -m64 $(INCLUDE) --std=c99 -Werror=implicit-function-declaration -g -O0 -w
LDFLAGS := -T linker.ld -nostdlib --allow-multiple-definition

all: $(ISO_DIR)/boot/kernel.elf grub

$(OBJ_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	@$(CC) $(CFLAGS) -c $< -o $@
	@echo "CC		$<"

$(OBJ_DIR)/%.o: %.S
	@mkdir -p $(dir $@)
	@$(AS) --64 $< -o $@
	@echo "AS		$<"

$(OBJ_DIR)/%.o: %.asm
	@mkdir -p $(dir $@)
	@$(NASM) -f elf64 $< -o $@
	@echo "NASM		$<"

$(ISO_DIR)/boot/kernel.elf: $(OBJ_DIR)/boot/boot.o $(filter-out $(OBJ_DIR)/boot/boot.o,$(OBJS))
	@mkdir -p $(ISO_DIR)/boot
	@$(LD) -n -T linker.ld -o $@ $^
	@echo "LD		$<"

grub:
	@mkdir -p $(ISO_DIR)/boot/grub
	@cp grub.cfg $(ISO_DIR)/boot/grub/grub.cfg
	@echo "Creating empty image, may be use sudo"
	@dd if=/dev/zero of=hatcher.img bs=1M count=64 status=none
	@parted hatcher.img --script mklabel msdos
	@parted hatcher.img --script mkpart primary fat32 2048s 100%
	@sudo losetup -Pf hatcher.img
	@LOOPDEV=$$(sudo losetup -j hatcher.img | cut -d: -f1); \
	PART=$${LOOPDEV}p1; \
	sudo mkfs.fat $$PART > /dev/null; \
	sudo mkdir -p /mnt/hatcher-fat; \
	sudo mount $$PART /mnt/hatcher-fat; \
	sudo cp -r $(ISO_DIR)/* /mnt/hatcher-fat/; \
	sudo grub-install --target=i386-pc --boot-directory=/mnt/hatcher-fat/boot --no-floppy --modules="part_msdos fat" $$LOOPDEV; \
	sudo umount /mnt/hatcher-fat; \
	sudo losetup -d $$LOOPDEV; \
	echo "Ready!"
	#@grub-mkrescue -o kernel.iso $(ISO_DIR)

clean:
	@rm -rf $(OBJ_DIR) $(ISO_DIR)/boot/kernel.elf hatcher.img hatcher.vmdk

convert_vmware:
	@qemu-img convert -f raw -O vmdk hatcher.img hatcher.vmdk
	@chmod 777 hatcher.vmdk

run:
	@qemu-system-x86_64 -drive file=hatcher.img,format=raw -drive file=../hda.img,format=raw -m 512M -boot d -audiodev sdl,id=pcspk_audio -machine pcspk-audiodev=pcspk_audio -debugcon stdio \
    -device virtio-scsi-pci,id=scsi0,bus=pci.0,addr=0x4 \

debug:
	@qemu-system-x86_64 -drive file=hatcher.img,format=raw -drive file=../hda.img,format=raw -m 512M -boot d -s -S -audiodev sdl,id=pcspk_audio -machine pcspk-audiodev=pcspk_audio -debugcon stdio

.PHONY: all clean grub run 