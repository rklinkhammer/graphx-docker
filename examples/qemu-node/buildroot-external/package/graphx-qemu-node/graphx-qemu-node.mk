################################################################################
# graphx-qemu-node
################################################################################

GRAPHX_QEMU_NODE_VERSION = 1.0.0
GRAPHX_QEMU_NODE_SITE = $(BR2_EXTERNAL_GRAPHX_QEMU_PATH)/../guest
GRAPHX_QEMU_NODE_SITE_METHOD = local

define GRAPHX_QEMU_NODE_BUILD_CMDS
	$(TARGET_CC) $(TARGET_CFLAGS) -std=c11 -Wall -Wextra -Werror \
		-D_POSIX_C_SOURCE=200809L $(@D)/src/qemu_network_node.c \
		-o $(@D)/qemu-network-node
endef

define GRAPHX_QEMU_NODE_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/qemu-network-node \
		$(TARGET_DIR)/usr/bin/qemu-network-node
endef

$(eval $(generic-package))
