GRAPHX_GUEST_VERSION = 1.1.0
GRAPHX_GUEST_SITE = /source
GRAPHX_GUEST_SITE_METHOD = local
GRAPHX_GUEST_LICENSE = MIT
GRAPHX_GUEST_LICENSE_FILES = LICENSE deps/yaml-cpp/LICENSE
GRAPHX_GUEST_DEPENDENCIES = openssl host-python3
GRAPHX_GUEST_CONF_OPTS = -DBUILD_SHARED_LIBS=OFF -DGRAPHX_BUILD_TESTS=OFF -DGRAPHX_BUILD_EXAMPLES=OFF -DGRAPHX_FORCE_BUNDLED_YAML_CPP=ON -DCMAKE_BUILD_TYPE=Release -DFETCHCONTENT_FULLY_DISCONNECTED=ON -DFETCHCONTENT_SOURCE_DIR_YAML-CPP=/source/deps/yaml-cpp

GRAPHX_GUEST_BUILD_OPTS = --target graphx-cli graphx-packet-guest
define GRAPHX_GUEST_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(GRAPHX_GUEST_BUILDDIR)/graphx $(TARGET_DIR)/usr/bin/graphx
	$(INSTALL) -D -m 0755 $(GRAPHX_GUEST_BUILDDIR)/graphx-packet-guest $(TARGET_DIR)/usr/bin/graphx-packet-guest
endef

define GRAPHX_GUEST_INSTALL_AGENT
	$(INSTALL) -D -m 0755 $(@D)/guests/common/agent.py $(TARGET_DIR)/usr/lib/graphx/agent.py
	printf '%s\n' '$(call qstrip,$(BR2_PACKAGE_GRAPHX_GUEST_APPLICATION))' > $(TARGET_DIR)/usr/lib/graphx/application
	mkdir -p $(TARGET_DIR)/usr/lib/graphx/sdr
	$(INSTALL) -m 0644 $(@D)/examples/sdr-node/common/*.py $(TARGET_DIR)/usr/lib/graphx/sdr/
	$(INSTALL) -m 0644 $(@D)/guests/common/radio.py $(TARGET_DIR)/usr/lib/graphx/sdr/radio.py
endef
GRAPHX_GUEST_POST_INSTALL_TARGET_HOOKS += GRAPHX_GUEST_INSTALL_AGENT
$(eval $(cmake-package))
