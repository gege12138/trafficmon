include $(TOPDIR)/rules.mk

PKG_NAME:=trafficmon
PKG_VERSION:=0.3.0
PKG_RELEASE:=9
PKG_LICENSE:=MIT
PKG_LICENSE_FILES:=LICENSE
PKG_BUILD_DIR:=$(BUILD_DIR)/$(PKG_NAME)-$(PKG_VERSION)
PKG_BUILD_PARALLEL:=1

include $(INCLUDE_DIR)/package.mk
include $(INCLUDE_DIR)/cmake.mk

define Package/trafficmon
  SECTION:=net
  CATEGORY:=Network
  TITLE:=Conntrack-based per-client traffic accounting
  DEPENDS:=+kmod-nf-conntrack-netlink
endef

define Package/trafficmon/description
 Small conntrack-based IPv4/IPv6 traffic accounting daemon and local CLI.
endef

define Package/trafficmon/conffiles
/etc/config/trafficmon
endef

CMAKE_OPTIONS += \
	-DBUILD_TESTING=OFF \
	-DCMAKE_BUILD_TYPE=MinSizeRel

TARGET_CFLAGS += -Wall -Wextra -Wpedantic -Werror

define Build/Prepare
	rm -rf $(PKG_BUILD_DIR)
	$(INSTALL_DIR) $(PKG_BUILD_DIR)
	$(CP) ./source/CMakeLists.txt $(PKG_BUILD_DIR)/
	$(CP) ./source/LICENSE $(PKG_BUILD_DIR)/
	$(CP) ./source/src $(PKG_BUILD_DIR)/
	$(CP) ./source/cli $(PKG_BUILD_DIR)/
endef

define Package/trafficmon/install
	$(INSTALL_DIR) $(1)/usr/sbin
	$(INSTALL_BIN) $(PKG_INSTALL_DIR)/usr/bin/trafficmon $(1)/usr/sbin/trafficmon
	$(INSTALL_BIN) $(PKG_INSTALL_DIR)/usr/bin/trafficctl $(1)/usr/sbin/trafficctl
	$(INSTALL_DIR) $(1)/etc/config
	$(INSTALL_CONF) ./files/trafficmon.config $(1)/etc/config/trafficmon
	$(INSTALL_DIR) $(1)/etc/init.d
	$(INSTALL_BIN) ./files/trafficmon.init $(1)/etc/init.d/trafficmon
	$(INSTALL_DIR) $(1)/etc/hotplug.d/iface
	$(INSTALL_BIN) ./files/95-trafficmon $(1)/etc/hotplug.d/iface/95-trafficmon
	$(INSTALL_DIR) $(1)/usr/lib/trafficmon
	$(INSTALL_DATA) ./files/network-signature.sh $(1)/usr/lib/trafficmon/network-signature.sh
	$(INSTALL_DIR) $(1)/usr/share/trafficmon
	$(INSTALL_CONF) ./files/protocols $(1)/usr/share/trafficmon/protocols
endef

$(eval $(call BuildPackage,trafficmon))
